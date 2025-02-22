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

#pragma once

#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL Serial
#endif

#include <SPI.h>

#include <CBUSLED.h>
#include <CBUSswitch.h>
#include <CBUSconfig.h>
#include <cbusdefs.h>

#define SW_TR_HOLD 6000U                   // CBUS push button hold time for SLiM/FLiM transition in millis = 6 seconds
#define DEFAULT_PRIORITY 0xB               // default CBUS messages priority. 1011 = 2|3 = normal/low
#define LONG_MESSAGE_DEFAULT_DELAY 20      // delay in milliseconds between sending successive long message fragments
#define LONG_MESSAGE_RECEIVE_TIMEOUT 5000  // timeout waiting for next long message packet
#define NUM_EX_CONTEXTS 4                  // number of send and receive contexts for extended implementation = number of concurrent messages
#define EX_BUFFER_LEN 64                   // size of extended send and receive buffers

//
/// CBUS modes
//

enum {
  MODE_SLIM = 0,
  MODE_FLIM = 1,
  MODE_CHANGING = 2
};

//
/// CBUS long message status codes
//

enum {
  CBUS_LONG_MESSAGE_INCOMPLETE = 0,
  CBUS_LONG_MESSAGE_COMPLETE,
  CBUS_LONG_MESSAGE_SEQUENCE_ERROR,
  CBUS_LONG_MESSAGE_TIMEOUT_ERROR,
  CBUS_LONG_MESSAGE_CRC_ERROR,
  CBUS_LONG_MESSAGE_TRUNCATED,
  CBUS_LONG_MESSAGE_INTERNAL_ERROR
};

//
/// CAN/CBUS message type
//

class CANFrame {

public:
  uint32_t id;
  bool ext;
  bool rtr;
  uint8_t len;
  uint8_t data[8] = {};
};

//
/// an abstract class to encapsulate CAN bus and CBUS processing
/// it must be implemented by a derived subclass
//

// forward references
class CBUSLongMessage;
class CBUScoe;

class CBUSbase {

public:
  CBUSbase();
  CBUSbase(CBUSConfig *the_config);

  // these methods are pure virtual and must be implemented by the derived class
  // as a consequence, it is not possible to create an instance of this class

#ifdef ARDUINO_ARCH_RP2040
  virtual bool begin(bool poll = false, SPIClassRP2040 & spi = SPI) = 0;
#else
  virtual bool begin(bool poll = false, SPIClass & spi = SPI) = 0;
#endif
  virtual bool available(void) = 0;
  virtual CANFrame getNextMessage(void) = 0;
  virtual bool sendMessage(CANFrame *msg, bool rtr = false, bool ext = false, byte priority = DEFAULT_PRIORITY) = 0;
  virtual bool sendMessageNoUpdate(CANFrame *msg) = 0;
  virtual void reset(void) = 0;

  // implementations of these methods are provided in the base class

  bool sendWRACK(void);
  bool sendCMDERR(byte cerrno);
  void CANenumeration(void);
  byte getCANID(unsigned long header);
  bool isExt(CANFrame *msg);
  bool isRTR(CANFrame *msg);
  void process(byte num_messages = 3);
  void process_single_message(CANFrame *msg);
  void initFLiM(void);
  void revertSLiM(void);
  void setSLiM(void);
  void renegotiate(void);
  void setLEDs(CBUSLED ledGrn, CBUSLED ledYlw);
  void setSwitch(CBUSSwitch sw);
  void setParams(unsigned char *mparams);
  void setName(unsigned char *mname);
  void checkCANenum(void);
  void indicateMode(byte mode);
  void setEventHandler(void (*fptr)(byte index, CANFrame *msg));
  void setEventHandler(void (*fptr)(byte index, CANFrame *msg, bool ison, byte evval));
  void setFrameHandler(void (*fptr)(CANFrame *msg), byte *opcodes = NULL, byte num_opcodes = 0);
  void setTransmitHandler(void (*fptr)(CANFrame *msg));
  void makeHeader(CANFrame *msg, byte priority = DEFAULT_PRIORITY);
  void processAccessoryEvent(unsigned int nn, unsigned int en, bool is_on_event);

  void setLongMessageHandler(CBUSLongMessage *handler);
  void consumeOwnEvents(CBUScoe *coe);

  unsigned int _numMsgsSent, _numMsgsRcvd;

protected:                                          // protected members become private in derived classes
  CANFrame _msg;
  CBUSLED _ledGrn, _ledYlw;
  CBUSSwitch _sw;
  CBUSConfig *module_config;
  unsigned char *_mparams;
  unsigned char *_mname;
  void (*eventhandler)(byte index, CANFrame *msg);
  void (*eventhandlerex)(byte index, CANFrame *msg, bool evOn, byte evVal);
  void (*framehandler)(CANFrame *msg);
  void (*transmithandler)(CANFrame *msg);
  byte *_opcodes;
  byte _num_opcodes;
  byte enum_responses[16];                          // 128 bits for storing CAN ID enumeration results
  bool bModeChanging, bCANenum, bLearn;
  unsigned long timeOutTimer, CANenumTime;
  bool enumeration_required;
  bool UI = false;

  CBUSLongMessage *longMessageHandler = nullptr;    // CBUS long message object to receive relevant frames
  CBUScoe *coe_obj = nullptr;                       // consume-own-events
};

//
/// a basic class to send and receive CBUS long messages per MERG RFC 0005
/// handles a single message, sending and receiving
/// suitable for small microcontrollers with limited memory
//

class CBUSLongMessage {

public:

  CBUSLongMessage(CBUSbase *cbus_object_ptr);
  bool sendLongMessage(const void *msg, const unsigned int msg_len, const byte stream_id, const byte priority = DEFAULT_PRIORITY);
  void subscribe(byte *stream_ids, const byte num_stream_ids, void *receive_buffer, const unsigned int receive_buffer_len, void (*messagehandler)(void *fragment, const unsigned int fragment_len, const byte stream_id, const byte status));
  bool process(void);
  virtual void processReceivedMessageFragment(const CANFrame *frame);
  bool is_sending(void);
  void setDelay(byte delay_in_millis);
  void setTimeout(unsigned int timeout_in_millis);

protected:

  bool sendMessageFragment(CANFrame *frame, const byte priority);

  bool _is_receiving = false;
  byte *_send_buffer, *_receive_buffer;
  byte _send_stream_id = 0, _receive_stream_id = 0, *_stream_ids = NULL, _num_stream_ids = 0, _send_priority = DEFAULT_PRIORITY, _msg_delay = LONG_MESSAGE_DEFAULT_DELAY, _sender_canid = 0;
  unsigned int _send_buffer_len = 0, _incoming_message_length = 0, _receive_buffer_len = 0, _receive_buffer_index = 0, _send_buffer_index = 0, _incoming_message_crc = 0, \
                                  _incoming_bytes_received = 0, _receive_timeout = LONG_MESSAGE_RECEIVE_TIMEOUT, _send_sequence_num = 0, _expected_next_receive_sequence_num = 0;
  unsigned long _last_fragment_sent = 0UL, _last_fragment_received = 0UL;

  void (*_messagehandler)(void *fragment, const unsigned int fragment_len, const byte stream_id, const byte status);        // user callback function to receive long message fragments
  CBUSbase *_cbus_object_ptr;
};


//// extended support for multiple concurrent long messages

// send and receive contexts

typedef struct _receive_context_t {
  bool in_use;
  byte receive_stream_id, sender_canid;
  byte *buffer;
  unsigned int receive_buffer_index, incoming_bytes_received, incoming_message_length, expected_next_receive_sequence_num, incoming_message_crc;
  unsigned long last_fragment_received;
} receive_context_t;

typedef struct _send_context_t {
  bool in_use, is_current;
  byte send_stream_id, send_priority, msg_delay;
  byte *buffer;
  unsigned int send_buffer_len, send_buffer_index, send_sequence_num, msg_crc;
  unsigned long last_fragment_sent, send_time;
} send_context_t;

//
/// a derived class to extend the base long message class to handle multiple concurrent messages, sending and receiving
//

class CBUSLongMessageEx : public CBUSLongMessage {

public:

  CBUSLongMessageEx(CBUSbase *cbus_object_ptr)
    : CBUSLongMessage(cbus_object_ptr) {}         // derived class constructor calls the base class constructor

  bool allocateContexts(byte num_receive_contexts, unsigned int receive_buffer_len, byte num_send_contexts);
  bool allocateContextsBuffers(byte num_receive_contexts, unsigned int receive_buffer_len, byte num_send_contexts, unsigned int send_buffer_len);
  bool sendLongMessage(const void *msg, const unsigned int msg_len, const byte stream_id, const byte priority = DEFAULT_PRIORITY);
  bool process(void);
  void subscribe(byte *stream_ids, const byte num_stream_ids, void (*messagehandler)(void *msg, unsigned int msg_len, byte stream_id, byte status));
  virtual void processReceivedMessageFragment(const CANFrame *frame);
  byte is_sending(void);
  bool is_sending_stream(byte stream_id);
  void use_crc(bool use_crc);
  void set_sequential(bool state);

private:

  bool _use_crc = false;
  bool _is_sequential = false;
  byte current_send_context, _num_receive_contexts = NUM_EX_CONTEXTS, _num_send_contexts = NUM_EX_CONTEXTS;
  receive_context_t **_receive_contexts = nullptr;
  send_context_t **_send_contexts = nullptr;
};

//
/// a circular buffer class
//

// buffer item type

typedef struct _buffer_entry2 {
  unsigned long _item_insert_time;
  CANFrame _item;
} buffer_entry2_t;

//

class circular_buffer2 {          // avoid name clash with implementation in CBUSMCP_CAN

public:
  circular_buffer2(byte num_items);
  ~circular_buffer2();
  bool available(void);
  void put(const CANFrame *cf);
  CANFrame *peek(void);
  CANFrame *get(void);
  unsigned long insert_time(void);
  bool full(void);
  void clear(void);
  bool empty(void);
  byte size(void);
  byte free_slots(void);
  unsigned int puts();
  unsigned int gets();
  byte hwm(void);
  unsigned int overflows(void);

private:
  bool _full;
  byte _head, _tail, _capacity, _size, _hwm;
  unsigned int _puts, _gets, _overflows;
  buffer_entry2_t *_buffer;
};

// consume-own-events class

class CBUScoe {

public:
  CBUScoe(const byte num_items = 4);
  ~CBUScoe();
  void put(const CANFrame *msg);
  CANFrame get(void);
  bool available(void);

private:
  circular_buffer2 *coe_buff;
};

//
/// pin set class, to encapsulate a set of 8 IO pins
//

class BoardIOPinSet {

public:
  BoardIOPinSet();
  BoardIOPinSet(byte *_pins);
  void setPins(byte *_pins);
  byte pin(byte pin_number);
  byte operator [] (byte i) const {return pin_array[i];}
  byte& operator [] (byte i) {return pin_array[i];}

private:
  byte pin_array[8];
};

//
/// base board class
//

class MainBoardBase {

public:
  MainBoardBase();
  ~MainBoardBase();
};

//
/// specific hardware classes
//

class Pico_Mainboard_rev_C : public MainBoardBase {

public:
  Pico_Mainboard_rev_C();
  ~Pico_Mainboard_rev_C();

  BoardIOPinSet upper;
  BoardIOPinSet lower;
  byte slim_led_pin = 21;
  byte flim_led_pin = 20;
  byte switch_pin = 22;
  byte cantx_pin = 1;
  byte canrx_pin = 2;

private:
  byte upper_pins[8] = {12, 11, 10, 9, 8, 7, 6, 0};
  byte lower_pins[8] = {28, 27, 26, 17, 16, 15, 14, 13};
};

//

class MegaAVR_mainboard_rev_C : public MainBoardBase {

public:
  MegaAVR_mainboard_rev_C();
  ~MegaAVR_mainboard_rev_C();

  BoardIOPinSet upper, lower;
  byte slim_led_pin = 22;
  byte flim_led_pin = 23;
  byte switch_pin = 24;
  byte cantx_pin = 255;
  byte canrx_pin = 255;

private:
  byte upper_pins[8] = {14, 15, 16, 17, 18, 19, 20, 21};
  byte lower_pins[8] = {28, 11, 10, 9, 8, 12, 13, 25};
};

//

class ESP32_mainboard_rev_C : public MainBoardBase {

public:
  ESP32_mainboard_rev_C();
  ~ESP32_mainboard_rev_C();

  BoardIOPinSet upper, lower;
  byte slim_led_pin = 22;
  byte flim_led_pin = 23;
  byte switch_pin = 24;
  byte cantx_pin = 255;
  byte canrx_pin = 255;

private:
  byte upper_pins[8] = {33, 0, 1, 3, 21, 19, 18, 5};
  byte lower_pins[8] = {32, 14, 12, 13, 17, 16, 23, 22};
};

//

class Nano_mainboard_rev_C : public MainBoardBase {

public:
  Nano_mainboard_rev_C();
  ~Nano_mainboard_rev_C();

  BoardIOPinSet upper, lower;
  byte slim_led_pin = 22;
  byte flim_led_pin = 23;
  byte switch_pin = 24;
  byte cantx_pin = 255;
  byte canrx_pin = 255;

private:
  byte upper_pins[8] = {14, 19, 18, 15, 16, 17, 3, 9};
  byte lower_pins[8] = {255, 255, 255, 255, 255, 255, 255, 255};
};

//

class AVRDA_mainboard_rev_C : public MainBoardBase {

public:
  AVRDA_mainboard_rev_C();
  ~AVRDA_mainboard_rev_C();

  BoardIOPinSet upper, lower;
  byte slim_led_pin = 22;
  byte flim_led_pin = 23;
  byte switch_pin = 24;
  byte cantx_pin = 255;
  byte canrx_pin = 255;

private:
  byte upper_pins[8] = {15, 11, 10, 9, 8, 12, 13, 14};
  byte lower_pins[8] = {255, 255, 255, 255, 255, 255, 255, 255};
};

//

