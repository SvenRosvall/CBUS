
/*

  Copyright (C) Duncan Greenwood 2017 (duncan_greenwood@hotmail.com)

  This work is licensed under the:
      Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit:
      http://creativecommons.org/licenses/by-nc-sa/4.0/
   or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

   License summary:
    You are free to:
      Share, copy and redistribute the material in any medium or format
      Adapt, remix, transform, and build upon the material

    The licensor cannot revoke these freedoms as long as you follow the license terms.

    Attribution : You must give appropriate credit, provide a link to the license,
                  and indicate if changes were made. You may do so in any reasonable manner,
                  but not in any way that suggests the licensor endorses you or your use.

    NonCommercial : You may not use the material for commercial purposes. **(see note below)

    ShareAlike : If you remix, transform, or build upon the material, you must distribute
                 your contributions under the same license as the original.

    No additional restrictions : You may not apply legal terms or technological measures that
                                 legally restrict others from doing anything the license permits.

   ** For commercial use, please contact the original copyright holder(s) to agree licensing terms

    This software is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE

*/

// 3rd party libraries
#include <Streaming.h>

// CBUS library
#include <CBUS.h>

// forward function declarations
void makeHeader_impl(CANFrame *msg, byte id, byte priority = 0x0b);

//
/// construct a CBUS object with an external CBUSConfig object named "config" that is defined
/// in user code
//

CBUSbase::CBUSbase() {
  extern CBUSConfig config;
  module_config = &config;
}

//
/// construct a CBUS object with a CBUSConfig object that the user provides.
/// note that this CBUSConfig object must have a lifetime longer than the CBUS object.
//

CBUSbase::CBUSbase(CBUSConfig *the_config) {
  module_config = the_config;
}

//
/// register the user handler for learned events
//

void CBUSbase::setEventHandler(void (*fptr)(byte index, CANFrame *msg)) {
  eventhandler = fptr;
}

// overloaded form which receives the opcode on/off state and the first event variable

void CBUSbase::setEventHandler(void (*fptr)(byte index, CANFrame *msg, bool ison, byte evval)) {
  eventhandlerex = fptr;
}

//
/// register the user handler for CAN frames
/// default args in .h declaration for opcodes array (NULL) and size (0)
//

void CBUSbase::setFrameHandler(void (*fptr)(CANFrame *msg), byte opcodes[], byte num_opcodes) {
  framehandler = fptr;
  _opcodes = opcodes;
  _num_opcodes = num_opcodes;
}

//
/// register a user handler for transmitted CAN message
//

void CBUSbase::setTransmitHandler(void (*fptr)(CANFrame *msg)) {
  transmithandler = fptr;
}

//
/// assign the module parameter set
//

void CBUSbase::setParams(unsigned char *mparams) {
  _mparams = mparams;
}

//
/// assign the module name
//

void CBUSbase::setName(unsigned char *mname) {
  _mname = mname;
}

//
/// set module to SLiM mode
//

void CBUSbase::setSLiM(void) {

  bModeChanging = false;
  module_config->setNodeNum(0);
  module_config->setFLiM(false);
  module_config->setCANID(0);

  indicateMode(module_config->FLiM);
}

//
/// extract CANID from CAN frame header
//

inline byte CBUSbase::getCANID(unsigned long header) {

  return header & 0x7f;
}

//
/// send a WRACK (write acknowledge) message
//

bool CBUSbase::sendWRACK(void) {

  // send a write acknowledgement response

  _msg.len = 3;
  _msg.data[0] = OPC_WRACK;
  _msg.data[1] = highByte(module_config->nodeNum);
  _msg.data[2] = lowByte(module_config->nodeNum);

  return sendMessage(&_msg);
}

//
/// send a CMDERR (command error) message
//

bool CBUSbase::sendCMDERR(byte cerrno) {

  // send a command error response

  _msg.len = 4;
  _msg.data[0] = OPC_CMDERR;
  _msg.data[1] = highByte(module_config->nodeNum);
  _msg.data[2] = lowByte(module_config->nodeNum);
  _msg.data[3] = cerrno;

  return sendMessage(&_msg);
}

//
/// is this an Extended CAN frame ?
//

bool CBUSbase::isExt(CANFrame *amsg) {

  return (amsg->ext);
}

//
/// is this a Remote frame ?
//

bool CBUSbase::isRTR(CANFrame *amsg) {

  return (amsg->rtr);
}

//
/// if in FLiM mode, initiate a CAN ID enumeration cycle
//

void CBUSbase::CANenumeration(void) {

  // initiate CAN bus enumeration cycle, either due to ENUM opcode, ID clash, or user button press

  // DEBUG_SERIAL << F("> beginning self-enumeration cycle") << endl;

  // set global variables
  bCANenum = true;                  // we are enumerating
  CANenumTime = millis();           // the cycle start time
  memset(enum_responses, 0, sizeof(enum_responses));

  // send zero-length RTR frame
  _msg.len = 0;
  sendMessage(&_msg, true, false);          // fixed arg order in v 1.1.4, RTR - true, ext = false

  // DEBUG_SERIAL << F("> enumeration cycle initiated") << endl;
  return;
}

//
/// initiate the transition from SLiM to FLiM mode
//

void CBUSbase::initFLiM(void) {

  // DEBUG_SERIAL << F("> initiating FLiM negotation") << endl;

  indicateMode(MODE_CHANGING);

  bModeChanging = true;
  timeOutTimer = millis();

  // send RQNN message with current NN, which may be zero if a virgin/SLiM node
  _msg.len = 3;
  _msg.data[0] = OPC_RQNN;
  _msg.data[1] = highByte(module_config->nodeNum);
  _msg.data[2] = lowByte(module_config->nodeNum);
  sendMessage(&_msg);

  // DEBUG_SERIAL << F("> requesting NN with RQNN message for NN = ") << module_config->nodeNum << endl;
  return;
}

//
/// revert from FLiM to SLiM mode
//

void CBUSbase::revertSLiM(void) {

  // DEBUG_SERIAL << F("> reverting to SLiM mode") << endl;

  // send NNREL message
  _msg.len = 3;
  _msg.data[0] = OPC_NNREL;
  _msg.data[1] = highByte(module_config->nodeNum);
  _msg.data[2] = lowByte(module_config->nodeNum);

  sendMessage(&_msg);
  setSLiM();
  return;
}

//
/// change or re-confirm node number
//

void CBUSbase::renegotiate(void) {

  initFLiM();
}

//
/// assign the two CBUS LED objects
//

void CBUSbase::setLEDs(CBUSLED green, CBUSLED yellow) {

  UI = true;
  _ledGrn = green;
  _ledYlw = yellow;

  return;
}

//
/// assign the CBUS pushbutton switch object
//

void CBUSbase::setSwitch(CBUSSwitch sw) {

  UI = true;
  _sw = sw;
}

//
/// set the CBUS LEDs to indicate the current mode
//

void CBUSbase::indicateMode(byte mode) {

  // DEBUG_SERIAL << F("> indicating mode = ") << mode << endl;

  if (UI) {
    switch (mode) {

    case MODE_FLIM:
      _ledYlw.on();
      _ledGrn.off();
      break;

    case MODE_SLIM:
      _ledYlw.off();
      _ledGrn.on();
      break;

    case MODE_CHANGING:
      _ledYlw.blink();
      _ledGrn.off();
      break;

    default:
      break;
    }
  }
}

/// main CBUS message processing procedure

void CBUSbase::process(byte num_messages) {

  // start bus enumeration if required
  if (enumeration_required) {
    enumeration_required = false;
    CANenumeration();
  }

  // process switch operations if the module is configured with one

  if (UI) {

    // allow LEDs to update
    _ledGrn.run();
    _ledYlw.run();

    // allow the CBUS switch some processing time
    _sw.run();

    //
    /// use LEDs to indicate that the user can release the switch
    //

    if (_sw.isPressed() && _sw.getCurrentStateDuration() > SW_TR_HOLD) {
      indicateMode(MODE_CHANGING);
    }

    //
    /// handle switch state changes
    //

    if (_sw.stateChanged()) {

      // has switch been released ?
      if (!_sw.isPressed()) {

        // how long was it pressed for ?
        unsigned long press_time = _sw.getLastStateDuration();

        // long hold > 6 secs
        if (press_time > SW_TR_HOLD) {
          // initiate mode change
          if (!module_config->FLiM) {
            initFLiM();
          } else {
            revertSLiM();
          }
        }

        // short 1-2 secs
        if (press_time >= 1000 && press_time < 2000) {
          renegotiate();
        }

        // very short < 0.5 sec
        if (press_time < 500 && module_config->FLiM) {
          CANenumeration();
        }

      } else {
        // do any switch release processing here
      }
    }
  }

  // get received CAN frames from buffer
  // process by default 3 messages per run so the user's application code doesn't appear unresponsive under load

  byte mcount = 0;

  while ((available() || (coe_obj != NULL && coe_obj->available())) && mcount < num_messages) {

    ++mcount;

    // at least one CAN frame is available in either the reception buffer or the COE buffer
    // retrieve the next one

    if (coe_obj != NULL && coe_obj->available()) {
      _msg = coe_obj->get();
    } else {
      _msg = getNextMessage();
    }

    //
    /// if registered, call the user handler with this new frame
    //

    if (framehandler != NULL) {

      // check if incoming opcode is in the user list, if list length > 0
      if (_num_opcodes > 0) {
        for (byte i = 0; i < _num_opcodes; i++) {
          if (_msg.data[0] == _opcodes[i]) {
            (void)(*framehandler)(&_msg);
            break;
          }
        }
      } else {
        (void)(*framehandler)(&_msg);
      }
    }

    // process just this message
    process_single_message(&_msg);
  }

  // check CAN bus enumeration timer
  checkCANenum();

  //
  /// check 30 sec timeout for SLiM/FLiM negotiation with FCU
  //

  if (bModeChanging && ((millis() - timeOutTimer) >= 30000)) {

    // DEBUG_SERIAL << F("> timeout expired, FLiM = ") << FLiM << F(", mode change = ") << bModeChanging << endl;
    indicateMode(module_config->FLiM);
    bModeChanging = false;
  }

  // DEBUG_SERIAL << F("> end of opcode processing, time = ") << (micros() - mtime) << "us" << endl;

  //
  /// end of CBUS message processing
  //
}

void CBUSbase::process_single_message(CANFrame *msg) {

  byte remoteCANID, evindex = 0, evval = 0, opc;
  unsigned int nn, en;

  // extract OPC, NN, EN
  opc = msg->data[0];
  nn = (msg->data[1] << 8) + msg->data[2];
  en = (msg->data[3] << 8) + msg->data[4];

  //
  /// extract the CANID of the sending module
  //

  remoteCANID = getCANID(msg->id);

  //
  /// pulse the green LED
  //

  if (UI) {
    _ledGrn.pulse();
  }

  //
  /// is this a CANID enumeration request from another node (zero-length and RTR set) ?
  //

  if (msg->rtr && msg->len == 0) {
    // DEBUG_SERIAL << F("> CANID enumeration RTR from CANID = ") << remoteCANID << endl;
    // send an empty message to show our CANID
    msg->len = 0;
    sendMessage(msg);
    return;
  }

  //
  /// set flag if we find a CANID conflict with the frame's producer
  /// doesn't apply to RTR or zero-length frames, so as not to trigger an enumeration loop
  //

  if (msg->len > 0 && remoteCANID == module_config->CANID && nn != module_config->nodeNum) {
    // DEBUG_SERIAL << F("> CAN id clash, enumeration required") << endl;
    enumeration_required = true;
  }

  // is this an extended frame ? we currently ignore these as bootloader, etc data may confuse us !
  if (msg->ext) {
    // DEBUG_SERIAL << F("> extended frame ignored, from CANID = ") << remoteCANID << endl;
    return;
  }

  // are we enumerating CANIDs ?
  if (bCANenum && msg->len == 0) {

    // store this response in the responses array
    if (remoteCANID > 0) {
      // fix to correctly record the received CANID
      bitWrite(enum_responses[(remoteCANID / 16)], remoteCANID % 8, 1);
      // DEBUG_SERIAL << F("> stored CANID ") << remoteCANID << F(" at index = ") << (remoteCANID / 8) << F(", bit = ") << (remoteCANID % 8) << endl;
    }

    return;
  }

  //
  /// process the message opcode
  /// if we got this far, it's a standard CAN frame (not extended, not RTR) with a data payload length > 0
  //

  if (msg->len > 0) {

    byte index;

    switch (opc) {

    case OPC_ACON:
    case OPC_ACON1:
    case OPC_ACON2:
    case OPC_ACON3:

    case OPC_ACOF:
    case OPC_ACOF1:
    case OPC_ACOF2:
    case OPC_ACOF3:

    case OPC_ARON:
    case OPC_AROF:

      // lookup this accessory event in the event table and call the user's registered callback function
      if (eventhandler || eventhandlerex) {
        _msg = *msg;
        processAccessoryEvent(nn, en, (opc % 2 == 0));
      }

      break;

    case OPC_ASON:
    case OPC_ASON1:
    case OPC_ASON2:
    case OPC_ASON3:

    case OPC_ASOF:
    case OPC_ASOF1:
    case OPC_ASOF2:
    case OPC_ASOF3:

      // lookup this accessory event in the event table and call the user's registered callback function
      // short events are stored with a node number of zero
      if (eventhandler || eventhandlerex) {
        _msg = *msg;
        processAccessoryEvent(0, en, (opc % 2 == 0));
      }

      break;

    case OPC_RQNP:
      // RQNP message - request for node paramters -- does not contain a NN or EN, so only respond if we
      // are in transition to FLiM
      // DEBUG_SERIAL << F("> RQNP -- request for node params during FLiM transition for NN = ") << nn << endl;

      // only respond if we are in transition to FLiM mode
      if (bModeChanging == true) {

        // DEBUG_SERIAL << F("> responding to RQNP with PARAMS") << endl;

        // respond with PARAMS message
        msg->len = 8;
        msg->data[0] = OPC_PARAMS;    // opcode
        msg->data[1] = _mparams[1];     // manf code -- MERG
        msg->data[2] = _mparams[2];     // minor code ver
        msg->data[3] = _mparams[3];     // module ident
        msg->data[4] = _mparams[4];     // number of events
        msg->data[5] = _mparams[5];     // events vars per event
        msg->data[6] = _mparams[6];     // number of NVs
        msg->data[7] = _mparams[7];     // major code ver
        // final param[8] = node flags is not sent here as the max message payload is 8 bytes (0-7)
        sendMessage(msg);

      }

      break;

    case OPC_RQNPN:
      // RQNPN message -- request parameter by index number
      // index 0 = number of params available;
      // respond with PARAN

      if (nn == module_config->nodeNum) {

        byte paran = msg->data[3];

        // DEBUG_SERIAL << F("> RQNPN request for parameter # ") << paran << F(", from nn = ") << nn << endl;

        if (paran <= _mparams[0]) {

          paran = msg->data[3];

          msg->len = 5;
          msg->data[0] = OPC_PARAN;
          // msg->data[1] = highByte(module_config->nodeNum);
          // msg->data[2] = lowByte(module_config->nodeNum);
          msg->data[3] = paran;
          msg->data[4] = _mparams[paran];
          sendMessage(msg);

        } else {
          // DEBUG_SERIAL << F("> RQNPN - param #") << paran << F(" is out of range !") << endl;
          sendCMDERR(9);
        }
      }

      break;

    case OPC_SNN:
      // received SNN - set node number
      // DEBUG_SERIAL << F("> received SNN with NN = ") << nn << endl;

      if (bModeChanging) {
        // DEBUG_SERIAL << F("> buf[1] = ") << msg->data[1] << ", buf[2] = " << msg->data[2] << endl;

        // save the NN
        // module_config->setNodeNum((msg->data[1] << 8) + msg->data[2]);
        module_config->setNodeNum(nn);

        // respond with NNACK
        msg->len = 3;
        msg->data[0] = OPC_NNACK;
        // msg->data[1] = highByte(module_config->nodeNum);
        // msg->data[2] = lowByte(module_config->nodeNum);

        sendMessage(msg);

        // DEBUG_SERIAL << F("> sent NNACK for NN = ") << module_config->nodeNum << endl;

        // we are now in FLiM mode - update the configuration
        bModeChanging = false;
        module_config->setFLiM(true);
        indicateMode(module_config->FLiM);

        // enumerate the CAN bus to allocate a free CAN ID
        CANenumeration();

        // DEBUG_SERIAL << F("> FLiM mode = ") << module_config->FLiM << F(", node number = ") << module_config->nodeNum << F(", CANID = ") << module_config->CANID << endl;

      } else {
        // DEBUG_SERIAL << F("> received SNN but not in transition") << endl;
      }

      break;

    case OPC_RQNN:
      // Another module has entered setup.
      // If we are in setup, abort as only one module can be in setup

      if (bModeChanging) {
        bModeChanging = false;
        indicateMode(module_config->FLiM);
        // respond with NNACK
        msg->len = 3;
        msg->data[0] = OPC_NNACK;
        msg->data[1] = highByte(module_config->nodeNum);
        msg->data[2] = lowByte(module_config->nodeNum);

        sendMessage(msg);
      }
      break;

    case OPC_CANID:
      // CAN -- set CANID
      // DEBUG_SERIAL << F("> CANID for nn = ") << nn << F(" with new CANID = ") << msg->data[3] << endl;

      if (nn == module_config->nodeNum) {
        // DEBUG_SERIAL << F("> setting my CANID to ") << msg->data[3] << endl;
        if (msg->data[3] < 1 || msg->data[3] > 99) {
          sendCMDERR(7);
        } else {
          module_config->setCANID(msg->data[3]);
        }
      }

      break;

    case OPC_ENUM:
      // received ENUM -- start CAN bus self-enumeration
      // DEBUG_SERIAL << F("> ENUM message for nn = ") << nn << F(" from CANID = ") << remoteCANID << endl;
      // DEBUG_SERIAL << F("> my nn = ") << module_config->nodeNum << endl;

      if (nn == module_config->nodeNum && remoteCANID != module_config->CANID && !bCANenum) {
        // DEBUG_SERIAL << F("> initiating enumeration") << endl;
        CANenumeration();
      }

      break;

    case OPC_NVRD:
      // received NVRD -- read NV by index
      if (nn == module_config->nodeNum) {

        byte nvindex = msg->data[3];
        if (nvindex > module_config->EE_NUM_NVS) {
          sendCMDERR(10);
        } else {
          // respond with NVANS
          msg->len = 5;
          msg->data[0] = OPC_NVANS;
          // msg->data[1] = highByte(module_config->nodeNum);
          // msg->data[2] = lowByte(module_config->nodeNum);
          msg->data[4] = module_config->readNV(nvindex);
          sendMessage(msg);
        }
      }

      break;

    case OPC_NVSET:
      // received NVSET -- set NV by index
      // DEBUG_SERIAL << F("> received NVSET for nn = ") << nn << endl;

      if (nn == module_config->nodeNum) {

        if (msg->data[3] > module_config->EE_NUM_NVS) {
          sendCMDERR(10);
        } else {
          // update EEPROM for this NV -- NVs are indexed from 1, not zero
          module_config->writeNV( msg->data[3], msg->data[4]);
          // respond with WRACK
          sendWRACK();
          // DEBUG_SERIAL << F("> set NV ok") << endl;
        }
      }

      break;

    case OPC_NNLRN:
      // received NNLRN -- place into learn mode
      // DEBUG_SERIAL << F("> NNLRN for node = ") << nn << F(", learn mode on") << endl;

      if (nn == module_config->nodeNum) {
        bLearn = true;
        // DEBUG_SERIAL << F("> set lean mode ok") << endl;
        // set bit 5 in parameter 8
        bitSet(_mparams[8], 5);
      }

      break;

    case OPC_EVULN:
      // received EVULN -- unlearn an event, by event number
      // en = (msg->data[3] << 8) + msg->data[4];
      // DEBUG_SERIAL << F("> EVULN for nn = ") << nn << F(", en = ") << en << endl;

      // we must be in learn mode
      if (bLearn == true) {

        // DEBUG_SERIAL << F("> searching for existing event to unlearn") << endl;

        // search for this NN and EN pair
        index = module_config->findExistingEvent(nn, en);

        if (index < module_config->EE_MAX_EVENTS) {

          // DEBUG_SERIAL << F("> deleting event at index = ") << index << F(", evs ") << endl;
          module_config->cleareventEEPROM(index);

          // update hash table
          module_config->updateEvHashEntry(index);

          // respond with WRACK
          sendWRACK();

        } else {
          // DEBUG_SERIAL << F("> did not find event to unlearn") << endl;
          // respond with CMDERR
          sendCMDERR(10);
        }
      } // if in learn mode

      break;

    case OPC_NNULN:
      // received NNULN -- exit from learn mode

      if (nn == module_config->nodeNum) {
        bLearn = false;
        // DEBUG_SERIAL << F("> NNULN for node = ") << nn << F(", learn mode off") << endl;
        // clear bit 5 in parameter 8
        bitClear(_mparams[8], 5);
      }

      break;

    case OPC_RQEVN:
      // received RQEVN -- request for number of stored events
      // DEBUG_SERIAL << F("> RQEVN -- number of stored events for nn = ") << nn << endl;

      if (nn == module_config->nodeNum) {

        // respond with 0x74 NUMEV
        msg->len = 4;
        msg->data[0] = OPC_NUMEV;
        // msg->data[1] = highByte(module_config->nodeNum);
        // msg->data[2] = lowByte(module_config->nodeNum);
        msg->data[3] = module_config->numEvents();

        sendMessage(msg);
      }

      break;

    case OPC_NERD:
      // request for all stored events
      // DEBUG_SERIAL << F("> NERD : request all stored events for nn = ") << nn << endl;

      if (nn == module_config->nodeNum) {
        msg->len = 8;
        msg->data[0] = OPC_ENRSP;                       // response opcode
        msg->data[1] = highByte(module_config->nodeNum);        // my NN hi
        msg->data[2] = lowByte(module_config->nodeNum);         // my NN lo

        for (byte i = 0; i < module_config->EE_MAX_EVENTS; i++) {

          if (module_config->getEvTableEntry(i) != 0) {
            // it's a valid stored event

            // read the event data from EEPROM
            // construct and send a ENRSP message
            module_config->readEvent(i, &msg->data[3]);
            msg->data[7] = i;                           // event table index

            // DEBUG_SERIAL << F("> sending ENRSP reply for event index = ") << i << endl;
            sendMessage(msg);
            delay(10);

          } // valid stored ev
        } // loop each ev
      } // for me

      break;

    case OPC_REVAL:
      // received REVAL -- request read of an event variable by event index and ev num
      // respond with NEVAL

      if (nn == module_config->nodeNum) {

        if (module_config->getEvTableEntry(msg->data[3]) != 0) {

          msg->len = 6;
          msg->data[0] = OPC_NEVAL;
          // msg->data[1] = highByte(module_config->nodeNum);
          // msg->data[2] = lowByte(module_config->nodeNum);
          msg->data[5] = module_config->getEventEVval(msg->data[3], msg->data[4]);
          sendMessage(msg);
        } else {

          // DEBUG_SERIAL << F("> request for invalid event index") << endl;
          sendCMDERR(6);
        }

      }

      break;

    case OPC_NNCLR:
      // NNCLR -- clear all stored events

      if (bLearn == true && nn == module_config->nodeNum) {

        // DEBUG_SERIAL << F("> NNCLR -- clear all events") << endl;

        for (byte e = 0; e < module_config->EE_MAX_EVENTS; e++) {
          module_config->cleareventEEPROM(e);
        }

        // recreate the hash table
        module_config->clearEvHashTable();
        // DEBUG_SERIAL << F("> cleared all events") << endl;

        sendWRACK();
      }

      break;

    case OPC_NNEVN:
      // request for number of free event slots

      if (module_config->nodeNum == nn) {

        byte free_slots = 0;

        // count free slots using the event hash table
        for (byte i = 0; i < module_config->EE_MAX_EVENTS; i++) {
          if (module_config->getEvTableEntry(i) == 0) {
            ++free_slots;
          }
        }

        // DEBUG_SERIAL << F("> responding to to NNEVN with EVNLF, free event table slots = ") << free_slots << endl;
        // memset(&_msg, 0, sizeof(_msg));
        msg->len = 4;
        msg->data[0] = OPC_EVNLF;
        // msg->data[1] = highByte(module_config->nodeNum);
        // msg->data[2] = lowByte(module_config->nodeNum);
        msg->data[3] = free_slots;
        sendMessage(msg);
      }

      break;

    case OPC_QNN:
      // this is probably a config recreate -- respond with PNN if we have a node number
      // DEBUG_SERIAL << F("> QNN received, my node number = ") << module_config->nodeNum << endl;

      if (module_config->nodeNum > 0) {
        // DEBUG_SERIAL << ("> responding with PNN message") << endl;
        msg->len = 6;
        msg->data[0] = OPC_PNN;
        msg->data[1] = highByte(module_config->nodeNum);
        msg->data[2] = lowByte(module_config->nodeNum);
        msg->data[3] = _mparams[1];
        msg->data[4] = _mparams[3];
        msg->data[5] = _mparams[8];
        sendMessage(msg);
      }

      break;

    case OPC_RQMN:
      // request for node module name, excluding "CAN" prefix
      // sent during module transition, so no node number check
      // DEBUG_SERIAL << F("> RQMN received") << endl;

      // only respond if in transition to FLiM

      // respond with NAME
      if (bModeChanging) {
        msg->len = 8;
        msg->data[0] = OPC_NAME;
        memcpy(msg->data + 1, _mname, 7);
        sendMessage(msg);
      }

      break;

    case OPC_EVLRN:
      // received EVLRN -- learn an event
      evindex = msg->data[5];
      evval = msg->data[6];

      // DEBUG_SERIAL << endl << F("> EVLRN for source nn = ") << nn << F(", en = ") << en << F(", evindex = ") << evindex << F(", evval = ") << evval << endl;

      // we must be in learn mode
      if (bLearn == true) {

        // search for this NN, EN as we may just be adding an EV to an existing learned event
        // DEBUG_SERIAL << F("> searching for existing event to update") << endl;
        index = module_config->findExistingEvent(nn, en);

        // not found - it's a new event
        if (index >= module_config->EE_MAX_EVENTS) {
          // DEBUG_SERIAL << F("> existing event not found - creating a new one if space available") << endl;
          index = module_config->findEventSpace();
        }

        // if existing or new event space found, write the event data

        if (index < module_config->EE_MAX_EVENTS) {

          // write the event to EEPROM at this location -- EVs are indexed from 1 but storage offsets start at zero !!
          // DEBUG_SERIAL << F("> writing EV = ") << evindex << F(", at index = ") << index << F(", offset = ") << (module_config->EE_EVENTS_START + (index * module_config->EE_BYTES_PER_EVENT)) << endl;

          // don't repeat this for subsequent EVs
          if (evindex < 2) {
            module_config->writeEvent(index, &msg->data[1]);

            // recreate event hash table entry
            // DEBUG_SERIAL << F("> updating hash table entry for idx = ") << index << endl;
            module_config->updateEvHashEntry(index);
          }

          module_config->writeEventEV(index, evindex, evval);

          // respond with WRACK
          sendWRACK();

        } else {
          // DEBUG_SERIAL << F("> no free event storage, index = ") << index << endl;
          // respond with CMDERR
          sendCMDERR(10);
        }

      } else { // bLearn == true
        // DEBUG_SERIAL << F("> error -- not in learn mode") << endl;
      }

      break;

    // case OPC_AREQ:
    //   // AREQ message - request for node state, only producer nodes

    //   if ((msg->data[1] == highByte(module_config->nodeNum)) && (msg->data[2] == lowByte(module_config->nodeNum))) {
    //     (void)(*eventhandler)(0, &_msg);
    //   }

    //   break;

    case OPC_BOOT:
      // boot mode
      break;

    case OPC_RSTAT:
      // command station status -- not applicable to accessory modules
      break;

    // case OPC_ARST:
    // system reset ... this is not what I thought it meant !
    // module_config->reboot();
    // break;

    case OPC_DTXC:
      // CBUS long message
      if (longMessageHandler != NULL) {
        longMessageHandler->processReceivedMessageFragment(msg);
      }
      break;

    default:
      // unknown or unhandled OPC
      // DEBUG_SERIAL << F("> opcode 0x") << _HEX(opc) << F(" is not currently implemented")  << endl;
      break;
    }
  } else {
    // DEBUG_SERIAL << F("> oops ... zero - length frame ?? ") << endl;
  }

  return;
}

void CBUSbase::checkCANenum(void) {

  //
  /// check the 100ms CAN enumeration cycle timer
  //

  byte selected_id = 1;     // default if no responses from other modules

  // if (bCANenum && !bCANenumComplete && (millis() - CANenumTime) >= 100) {
  if (bCANenum && (millis() - CANenumTime) >= 100) {

    // enumeration timer has expired -- stop enumeration and process the responses

    // DEBUG_SERIAL << F("> enum cycle complete at ") << millis() << F(", start = ") << CANenumTime << F(", duration = ") << (millis() - CANenumTime) << endl;
    // DEBUG_SERIAL << F("> processing received responses") << endl;

    // iterate through the 128 bit field
    for (byte i = 0; i < 16; i++) {

      // ignore if this byte is all 1's -> there are no unused IDs in this group of numbers
      if (enum_responses[i] == 0xff) {
        continue;
      }

      // for each bit in the byte
      for (byte b = 0; b < 8; b++) {

        // ignore first bit of first byte -- CAN ID zero is not used for nodes
        if (i == 0 && b == 0) {
          continue;
        }

        // if the bit is not set
        if (bitRead(enum_responses[i], b) == 0) {
          selected_id = ((i * 16) + b);
          // DEBUG_SERIAL << F("> bit ") << b << F(" of byte ") << i << F(" is not set, first free CAN ID = ") << selected_id << endl;
          // i = 16; // ugh ... but probably better than a goto :)
          // but using a goto saves 4 bytes of program size ;)
          goto check_done;
          break;
        }
      }
    }

check_done:

    // DEBUG_SERIAL << F("> enumeration responses = ") << enums << F(", lowest available CAN id = ") << selected_id << endl;

    // bCANenumComplete = true;
    bCANenum = false;
    CANenumTime = 0UL;

    // store the new CAN ID
    module_config->setCANID(selected_id);

    // send NNACK
    // _msg.len = 3;
    // _msg.data[0] = OPC_NNACK;
    // _msg.data[1] = highByte(module_config->nodeNum);
    // _msg.data[2] = lowByte(module_config->nodeNum);
    // sendMessage(&_msg);
  }
}

//
/// for accessory event messages, lookup the event in the event table and call the user's registered event handler function
//

void CBUSbase::processAccessoryEvent(unsigned int nn, unsigned int en, bool is_on_event) {

  // try to find a matching stored event -- match on nn, en
  byte index = module_config->findExistingEvent(nn, en);

  // call any registered event handler

  if (index < module_config->EE_MAX_EVENTS) {
    if (eventhandler != NULL) {
      (void)(*eventhandler)(index, &_msg);
    } else if (eventhandlerex != NULL) {
      (void)(*eventhandlerex)(index, &_msg, is_on_event, \
                              ((module_config->EE_NUM_EVS > 0) ? module_config->getEventEVval(index, 1) : 0) \
                             );
    }
  }
}

//
/// set the long message handler object to receive long message frames
//

void CBUSbase::setLongMessageHandler(CBUSLongMessage *handler) {
  longMessageHandler = handler;
}

void CBUSbase::consumeOwnEvents(CBUScoe *coe) {
  coe_obj = coe;
}

//
/// utility method to populate a CBUS message header
//

void CBUSbase::makeHeader(CANFrame *msg, byte priority) {

  makeHeader_impl(msg, module_config->CANID, priority);
  return;
}

//
/// actual implementation of the makeHeader method
/// so it can be called directly or as a CBUS class method
/// the 11 bit ID of a standard CAN frame is comprised of: (4 bits of CBUS priority) + (7 bits of CBUS CAN ID)
/// priority = 1011 (0xB hex, 11 dec) as default argument, which translates to medium/low
//

void makeHeader_impl(CANFrame *msg, byte id, byte priority) {

  msg->id = (priority << 7) + (id & 0x7f);
  return;
}

//
/// consume own events class
//

CBUScoe::CBUScoe(const byte num_items) {

  coe_buff = new circular_buffer2(num_items);
}

CBUScoe::~CBUScoe() {

  free(coe_buff);
}

void CBUScoe::put(const CANFrame *msg) {

  coe_buff->put(msg);
}

bool CBUScoe::available(void) {

  return coe_buff->available();
}

CANFrame CBUScoe::get(void) {

  CANFrame msg;
  memcpy(&msg, coe_buff->get(), sizeof(CANFrame));
  return msg;
}

///
/// a circular buffer class
///

/// constructor and destructor

circular_buffer2::circular_buffer2(byte num_items) {

  _head = 0;
  _tail = 0;
  _hwm = 0;
  _capacity = num_items;
  _size = 0;
  _puts = 0;
  _gets = 0;
  _overflows = 0;
  _full = false;
  _buffer = (buffer_entry2_t *)malloc(num_items * sizeof(buffer_entry2_t));
}

circular_buffer2::~circular_buffer2() {
  free(_buffer);
}

/// if buffer has one or more stored items

bool circular_buffer2::available(void) {

  return (_size > 0);
}

/// store an item to the buffer - overwrite oldest item if buffer is full
/// never called from an interrupt context so we don't need to worry about interrupts

void circular_buffer2::put(const CANFrame * item) {

  memcpy((CANFrame*)&_buffer[_head]._item, (const CANFrame *)item, sizeof(CANFrame));
  _buffer[_head]._item_insert_time = micros();

  // if the buffer is full, this put will overwrite the oldest item

  if (_full) {
    _tail = (_tail + 1) % _capacity;
    ++_overflows;
  }

  _head = (_head + 1) % _capacity;
  _full = _head == _tail;
  _size = size();
  _hwm = (_size > _hwm) ? _size : _hwm;
  ++_puts;

  return;
}

/// retrieve the next item from the buffer

CANFrame *circular_buffer2::get(void) {

  CANFrame *p = nullptr;

  // should always call ::available first to avoid returning null pointer

  if (_size > 0) {
    p = &_buffer[_tail]._item;
    _full = false;
    _tail = (_tail + 1) % _capacity;
    _size = size();
    ++_gets;
  }

  return p;
}

/// get the insert time of the current buffer tail item
/// must be called before the item is removed by ::get

unsigned long circular_buffer2::insert_time(void) {

  return (_buffer[_tail]._item_insert_time);
}

/// peek at the next item in the buffer without removing it

CANFrame *circular_buffer2::peek(void) {

  // should always call ::available first to avoid this

  if (_size == 0) {
    return nullptr;
  }

  return (&_buffer[_tail]._item);
}

/// clear all items

void circular_buffer2::clear(void) {

  _head = 0;
  _tail = 0;
  _full = false;
  _size = 0;

  return;
}

/// return high water mark

byte circular_buffer2::hwm(void) {

  return _hwm;
}

/// return full indicator

bool circular_buffer2::full(void) {

  return _full;
}

/// recalculate number of items in the buffer

byte circular_buffer2::size(void) {

  byte size = _capacity;

  if (!_full) {
    if (_head >= _tail) {
      size = _head - _tail;
    } else {
      size = _capacity + _head - _tail;
    }
  }

  _size = size;
  return _size;
}

/// return empty indicator

bool circular_buffer2::empty(void) {

  return (!_full && (_head == _tail));
}

/// return number of free slots

byte circular_buffer2::free_slots(void) {

  return (_capacity - _size);
}

/// number of puts

unsigned int circular_buffer2::puts(void) {

  return _puts;
}

/// number of gets

unsigned int circular_buffer2::gets(void) {

  return _gets;
}

/// number of overflows

unsigned int circular_buffer2::overflows(void) {

  return _overflows;
}

