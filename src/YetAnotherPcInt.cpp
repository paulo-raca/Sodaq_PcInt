/*
 * YetAnotherPcInt.cpp
 *
 * Copyright (c) 2014-2016 Kees Bakker, Paulo Costa
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA  02111-1307  USA
 *
 * This module supplies a set of helper functions to use the
 * PinChange interrupt in a convenient manner, similar to
 * the standard Arduino attachInterrupt.  It was created with
 * inspiration from PcInt, PinChangeInt and PciManager.  The main
 * goal was to keep it simple and small as possible.
 */

#include "YetAnotherPcInt.h"
#include "PinChangeInterruptBoards.h"

#define WITHOUT_INTERRUPTION(CODE) {uint8_t sreg = SREG; noInterrupts(); {CODE} SREG = sreg;}

#define IMPLEMENT_ISR(port, isr_vect, pcmsk, input) \
  ISR(isr_vect) \
  { \
    uint8_t new_state = input; \
    uint8_t trigger_pins = pcmsk & (port.state ^ new_state) & ( (port.rising & new_state) | (port.falling & ~new_state) ); \
    PcIntCallback* callbacks = port.callbacks; \
    port.state = new_state; \
    while (trigger_pins) { \
      if ((trigger_pins & 1)) { \
        callbacks->func(callbacks->arg, new_state & 1); \
      } \
      new_state >>=1; \
      trigger_pins >>= 1; \
      callbacks++; \
    } \
  }

struct PcIntCallback {
  PcInt::callback func;
  void* arg;
};
  
struct PcIntPort {
  PcIntCallback callbacks[8];
  uint8_t state;
  uint8_t rising;
  uint8_t falling;
};

#if defined(PCINT_INPUT_PORT0)
PcIntPort port0;
IMPLEMENT_ISR(port0, PCINT0_vect, PCMSK0, PCINT_INPUT_PORT0)
#endif
#if defined(PCINT_INPUT_PORT1)
PcIntPort port1;
IMPLEMENT_ISR(port1, PCINT1_vect, PCMSK1, PCINT_INPUT_PORT1)
#endif
#if defined(PCINT_INPUT_PORT2)
PcIntPort port2;
IMPLEMENT_ISR(port2, PCINT2_vect, PCMSK2, PCINT_INPUT_PORT2)
#endif
#if defined(PCINT_INPUT_PORT3)
PcIntPort port3;
IMPLEMENT_ISR(port3, PCINT3_vect, PCMSK3, PCINT_INPUT_PORT3)
#endif


static inline PcIntPort* get_port(uint8_t port) {
    switch (port) {
#if defined(PCINT_INPUT_PORT0)
        case 0: return &port0;
#endif
#if defined(PCINT_INPUT_PORT1)
        case 1: return &port1;
#endif
#if defined(PCINT_INPUT_PORT2)
        case 2: return &port2;
#endif
#if defined(PCINT_INPUT_PORT3)
        case 3: return &port3;
#endif
        default: return nullptr;
    }
}

static inline uint8_t get_port_value(uint8_t port) {
    switch (port) {
#if defined(PCINT_INPUT_PORT0)
        case 0: return PCINT_INPUT_PORT0;
#endif
#if defined(PCINT_INPUT_PORT1)
        case 1: return PCINT_INPUT_PORT1;
#endif
#if defined(PCINT_INPUT_PORT2)
        case 2: return PCINT_INPUT_PORT2;
#endif
#if defined(PCINT_INPUT_PORT3)
        case 3: return PCINT_INPUT_PORT3;
#endif
        default: return 0;
    }
}

void PcInt::attachInterrupt(uint8_t pin, callback func, void* arg, uint8_t mode) {
  volatile uint8_t * pcicr = digitalPinToPCICR(pin);
  volatile uint8_t * pcmsk = digitalPinToPCMSK(pin);
  uint8_t portGroup = digitalPinToPCICRbit(pin);
  uint8_t portBit = digitalPinToPCMSKbit(pin);
  uint8_t portBitMask = _BV(portBit);
  PcIntPort* port = get_port(portGroup);
  
  if (pcicr && pcmsk && port && func) {
    WITHOUT_INTERRUPTION({
      port->callbacks[portBit].func = func;
      port->callbacks[portBit].arg  = arg;
      port->rising  = (mode == RISING || mode == CHANGE)  ?  (port->rising  | portBitMask)  :  (port->rising  & ~portBitMask);
      port->falling = (mode == FALLING|| mode == CHANGE)  ?  (port->falling | portBitMask)  :  (port->falling & ~portBitMask);
      *pcmsk |= portBitMask;
      *pcicr |= _BV(digitalPinToPCICRbit(pin));

      //Update the current state (but only for this pin, to prevent concurrency issues on the others)
      port->state = (port->state & ~portBitMask) | (get_port_value(portGroup) & portBitMask);
    })
  }
}

void PcInt::detachInterrupt(uint8_t pin) {
  volatile uint8_t * pcicr = digitalPinToPCICR(pin);
  volatile uint8_t * pcmsk = digitalPinToPCMSK(pin);
  uint8_t portGroup = digitalPinToPCICRbit(pin);
  uint8_t portBit = digitalPinToPCMSKbit(pin);
  uint8_t portBitMask = _BV(portBit);
  PcIntPort* port = get_port(portGroup);
  
  if (pcicr && pcmsk && port) {
    WITHOUT_INTERRUPTION({
      port->callbacks[portBit].func = nullptr;
      port->callbacks[portBit].arg  = nullptr;
      port->rising &= ~portBitMask;
      port->falling &= ~portBitMask; 

      *pcmsk &= ~portBitMask;
      //Switch off the group if all of the group are now off
      if (!*pcmsk) {
        *pcicr &= ~_BV(digitalPinToPCICRbit(pin));
      }
    })
  }
}

void PcInt::enableInterrupt(uint8_t pin) {
  volatile uint8_t * pcicr = digitalPinToPCICR(pin);
  volatile uint8_t * pcmsk = digitalPinToPCMSK(pin);
  uint8_t portGroup = digitalPinToPCICRbit(pin);
  uint8_t portBit = digitalPinToPCMSKbit(pin);
  uint8_t portBitMask = _BV(portBit);
  PcIntPort* port = get_port(portGroup);
  
  if (pcicr && pcmsk && port && port->callbacks[portBit].func && !(*pcmsk & portBitMask)) {
    WITHOUT_INTERRUPTION({
      *pcmsk |= portBitMask;
      *pcicr |= _BV(digitalPinToPCICRbit(pin));

      //Update the current state (but only for this pin, to prevent concurrency issues on the others)
      port->state = (port->state & ~portBitMask) | (get_port_value(portGroup) & portBitMask);
    })
  }
}

void PcInt::disableInterrupt(uint8_t pin) {
  volatile uint8_t * pcicr = digitalPinToPCICR(pin);
  volatile uint8_t * pcmsk = digitalPinToPCMSK(pin);
  uint8_t portBit = digitalPinToPCMSKbit(pin);
  uint8_t portBitMask = _BV(portBit);
  
  if (pcicr && pcmsk) {
    WITHOUT_INTERRUPTION({
      *pcmsk &= ~portBitMask;
      //Switch off the group if all of the group are now off
      if (!*pcmsk) {
        *pcicr &= ~_BV(digitalPinToPCICRbit(pin));
      }
    })
  }
}
