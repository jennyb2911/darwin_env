/*
 * Copyright (c) 1998-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * 18 June 1998 sdouglas  Start IOKit version.
 * 18 Nov  1998 suurballe port to C++
 *  4 Oct  1999 decesare  Revised for Type 4 support and sub-classed drivers.
 */

#include <IOKit/adb/IOADBDevice.h>
#include <IOKit/hidsystem/IOHIPointing.h>
#include <IOKit/hid/IOHIDDevice.h>

#define TRUE	1
#define FALSE	0

class AppleADBMouse: public IOHIPointing
{
  OSDeclareDefaultStructors(AppleADBMouse);

protected:
  IOADBDevice * adbDevice;
  IOFixed       _resolution;
  IOItemCount   _buttonCount;
  
public:
  virtual IOService * probe(IOService * provider, SInt32 * score);
  virtual bool start(IOService * provider);
  virtual UInt32 interfaceID(void);
  virtual UInt32 deviceType(void);
  virtual IOFixed resolution(void);
  virtual IOItemCount buttonCount(void);
  virtual void packet(UInt8 adbCommand, IOByteCount length, UInt8 * data);
};


class AppleADBMouseType1 : public AppleADBMouse
{
  OSDeclareDefaultStructors(AppleADBMouseType1);
  
public:
  virtual IOService * probe(IOService * provider, SInt32 * score);
  virtual bool start(IOService * provider);
};


class AppleADBMouseType2 : public AppleADBMouse
{
  OSDeclareDefaultStructors(AppleADBMouseType2);
  
public:
  virtual IOService * probe(IOService * provider, SInt32 * score);
  virtual bool start(IOService * provider);
};


class AppleADBMouseType4 : public AppleADBMouse
{
  OSDeclareDefaultStructors(AppleADBMouseType4);

private:
    bool Clicking, Dragging, DragLock, typeTrackpad;
    bool _jitterclick, _jittermove;
    bool _palmnomove, _palmnoaction, _2fingernoaction, _sticky2finger, _zonenomove, 
	_zonenoaction, _zonepecknomove, _ignorezone, _isWEnhanced, _usePantherSettings;
    UInt8   _WThreshold;
    UInt16  _jitterdelta;
    UInt32  _ignoreTrackpadState, _oldButtonState;
    UInt64  _jitterclicktime64, _zonepeckingtime64, _timeoutSticky64, _sticky2fingerTime64,
	    _fake5min;
    AbsoluteTime  _jitterclicktimeAB, _zonepeckingtimeAB, _timeoutStickyAB, _sticky2fingerTimeAB,
	    _fake5minAB, _keyboardTimeAB;

    virtual IOReturn setParamProperties( OSDictionary * dict );
    bool enableEnhancedMode();
    IOService *_pADBKeyboard;
    IONotifier * _notifierA, * _notifierT;
    const OSSymbol 	*_gettime;
    IOLock *		_mouseLock;
    OSSet *             _externalMice;

protected:
  UInt32 deviceSignature;
  UInt16 deviceResolution;
  UInt8  deviceClass;
  UInt8  deviceNumButtons;
  
public:
  virtual IOService * probe(IOService * provider, SInt32 * score);
  virtual bool start(IOService * provider);
  virtual void packet(UInt8 adbCommand, IOByteCount length, UInt8 * data);
  virtual void packetW(UInt8 adbCommand, IOByteCount length, UInt8 * data);
  virtual void packetWP(UInt8 adbCommand, IOByteCount length, UInt8 * data);
  virtual OSData * copyAccelerationTable();
  virtual void _check_usb_mouse(IOService * service, bool added); 
  virtual void free();

};
