/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/audio/IOAudioPort.h>
#include <IOKit/audio/IOAudioTypes.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSSet.h>
#include <libkern/c++/OSCollectionIterator.h>

#define NUM_POWER_STATES	2

class IOAudioTimerEvent : public OSObject
{
    friend class IOAudioDevice;

    OSDeclareDefaultStructors(IOAudioTimerEvent)

protected:
    OSObject *	target;
    IOAudioDevice::TimerEvent event;
    AbsoluteTime interval;
};

OSDefineMetaClassAndStructors(IOAudioTimerEvent, OSObject)

class IOAudioEngineEntry : public OSObject
{
    friend class IOAudioDevice;

    OSDeclareDefaultStructors(IOAudioEngineEntry);

protected:
    IOAudioEngine *audioEngine;
    bool shouldStopAudioEngine;
};

OSDefineMetaClassAndStructors(IOAudioEngineEntry, OSObject)

#define super IOService
OSDefineMetaClassAndStructors(IOAudioDevice, IOService)
OSMetaClassDefineReservedUsed(IOAudioDevice, 0);
OSMetaClassDefineReservedUsed(IOAudioDevice, 1);
OSMetaClassDefineReservedUsed(IOAudioDevice, 2);
OSMetaClassDefineReservedUsed(IOAudioDevice, 3);

OSMetaClassDefineReservedUnused(IOAudioDevice, 4);
OSMetaClassDefineReservedUnused(IOAudioDevice, 5);
OSMetaClassDefineReservedUnused(IOAudioDevice, 6);
OSMetaClassDefineReservedUnused(IOAudioDevice, 7);
OSMetaClassDefineReservedUnused(IOAudioDevice, 8);
OSMetaClassDefineReservedUnused(IOAudioDevice, 9);
OSMetaClassDefineReservedUnused(IOAudioDevice, 10);
OSMetaClassDefineReservedUnused(IOAudioDevice, 11);
OSMetaClassDefineReservedUnused(IOAudioDevice, 12);
OSMetaClassDefineReservedUnused(IOAudioDevice, 13);
OSMetaClassDefineReservedUnused(IOAudioDevice, 14);
OSMetaClassDefineReservedUnused(IOAudioDevice, 15);
OSMetaClassDefineReservedUnused(IOAudioDevice, 16);
OSMetaClassDefineReservedUnused(IOAudioDevice, 17);
OSMetaClassDefineReservedUnused(IOAudioDevice, 18);
OSMetaClassDefineReservedUnused(IOAudioDevice, 19);
OSMetaClassDefineReservedUnused(IOAudioDevice, 20);
OSMetaClassDefineReservedUnused(IOAudioDevice, 21);
OSMetaClassDefineReservedUnused(IOAudioDevice, 22);
OSMetaClassDefineReservedUnused(IOAudioDevice, 23);
OSMetaClassDefineReservedUnused(IOAudioDevice, 24);
OSMetaClassDefineReservedUnused(IOAudioDevice, 25);
OSMetaClassDefineReservedUnused(IOAudioDevice, 26);
OSMetaClassDefineReservedUnused(IOAudioDevice, 27);
OSMetaClassDefineReservedUnused(IOAudioDevice, 28);
OSMetaClassDefineReservedUnused(IOAudioDevice, 29);
OSMetaClassDefineReservedUnused(IOAudioDevice, 30);
OSMetaClassDefineReservedUnused(IOAudioDevice, 31);

// New code added here
void IOAudioDevice::setDeviceTransportType(const UInt32 transportType)
{
    if (transportType) {
        setProperty(kIOAudioDeviceTransportTypeKey, transportType, 32);
    }
}

// This needs to be overridden by driver if it wants to know about power manager changes.
// If overridden, be sure to still call super::setAggressiveness() so we can call our parent.
IOReturn IOAudioDevice::setAggressiveness(unsigned long type, unsigned long newLevel)
{
	return super::setAggressiveness(type, newLevel);
}

void IOAudioDevice::setIdleAudioSleepTime(unsigned long long sleepDelay)
{
	assert(reserved);

#ifdef DEBUG_CALLS
	IOLog ("IOAudioDevice[%p]::setIdleAudioSleepTime: sleepDelay = %lx%lx\n", this, (UInt32)(sleepDelay >> 32), (UInt32)sleepDelay);
#endif
	if (reserved->idleSleepDelayTime != sleepDelay) {
		reserved->idleSleepDelayTime = sleepDelay;
		scheduleIdleAudioSleep();
	}

	if (reserved->idleSleepDelayTime == kNoIdleAudioPowerDown) {
		if (reserved->idleTimer) {
			reserved->idleTimer->cancelTimeout();
		}
	}
}

// Set up a timer to power down the hardware if we haven't used it in a while.
void IOAudioDevice::scheduleIdleAudioSleep(void)
{
    AbsoluteTime				fireTime;
    UInt64						nanos;

	assert(reserved);

#ifdef DEBUG_CALLS
	IOLog ("IOAudioDevice[%p]::scheduleIdleAudioSleep: idleSleepDelayTime = %lx%lx\n", this, (UInt32)(reserved->idleSleepDelayTime >> 32), (UInt32)reserved->idleSleepDelayTime);
#endif
	if (reserved->idleSleepDelayTime == 0) {
		// For backwards compatibility, or drivers that don't care, tell them about idle right away.
		initiatePowerStateChange();
	} else {
		if (!reserved->idleTimer && reserved->idleSleepDelayTime != kNoIdleAudioPowerDown) {
			reserved->idleTimer = IOTimerEventSource::timerEventSource(this, idleAudioSleepHandlerTimer);
			if (!reserved->idleTimer) {
				return;
			}
			workLoop->addEventSource(reserved->idleTimer);
		}
	
		if (reserved->idleSleepDelayTime != kNoIdleAudioPowerDown) {
			// If the driver wants to know about idle sleep after a specific amount of time, then set the timer to tell them at that time.
			// If idleSleepDelayTime == 0xffffffff then don't ever tell the driver about going idle
			clock_get_uptime(&fireTime);
			absolutetime_to_nanoseconds(fireTime, &nanos);
			nanos += reserved->idleSleepDelayTime;
			nanoseconds_to_absolutetime(nanos, &fireTime);
			reserved->idleTimer->wakeAtTime(fireTime);		// will call idleAudioSleepHandlerTimer
		}
	}

	return;
}

void IOAudioDevice::idleAudioSleepHandlerTimer(OSObject *owner, IOTimerEventSource *sender)
{
	IOAudioDevice *				audioDevice;

	audioDevice = OSDynamicCast(IOAudioDevice, owner);
	assert(audioDevice);

#ifdef DEBUG_CALLS
	IOLog ("IOAudioDevice[%p]idleAudioSleepHandlerTimer: pendingPowerState = %d, idleSleepDelayTime = %lx%lx\n", audioDevice, audioDevice->pendingPowerState, (UInt32)(audioDevice->reserved->idleSleepDelayTime >> 32), (UInt32)audioDevice->reserved->idleSleepDelayTime);
#endif
	if (audioDevice->reserved->idleSleepDelayTime != kNoIdleAudioPowerDown &&
		audioDevice->getPendingPowerState () == kIOAudioDeviceIdle) {
		// If we're still idle, tell the device to go idle now that the requested amount of time has elapsed.
		audioDevice->initiatePowerStateChange();
	}

	return;
}

void IOAudioDevice::setConfigurationApplicationBundle(const char *bundleID)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::setConfigurationApplicationBundle(%p)\n", this, bundleID);
#endif

    if (bundleID) {
        setProperty(kIOAudioDeviceConfigurationAppKey, bundleID);
    }
}

// Original code here...
const IORegistryPlane *IOAudioDevice::gIOAudioPlane = 0;

bool IOAudioDevice::init(OSDictionary *properties)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::init(%p)\n", this, properties);
#endif

    if (!super::init(properties)) {
        return false;
    }

	reserved = (ExpansionData *)IOMalloc (sizeof(struct ExpansionData));
	if (!reserved) {
		return false;
	} else {
		reserved->idleSleepDelayTime = 0;
		reserved->idleTimer = NULL;
	}

    if (!gIOAudioPlane) {
        gIOAudioPlane = IORegistryEntry::makePlane(kIOAudioPlane);
    }

    audioEngines = OSArray::withCapacity(2);
    if (!audioEngines) {
        return false;
    }

    audioPorts = OSSet::withCapacity(1);
    if (!audioPorts) {
        return false;
    }

    workLoop = IOWorkLoop::workLoop();
    if (!workLoop) {
        return false;
    }
    
    familyManagePower = true;
    asyncPowerStateChangeInProgress = false;
    
    currentPowerState = kIOAudioDeviceIdle;
    pendingPowerState = kIOAudioDeviceIdle;
    
    numRunningAudioEngines = 0;
    
    return true;
}

void IOAudioDevice::free()
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::free()\n", this);
#endif

    if (audioEngines) {
        deactivateAllAudioEngines();
        audioEngines->release();
        audioEngines = 0;
    }

    if (audioPorts) {
        detachAllAudioPorts();
        audioPorts->release();
        audioPorts = 0;
    }
    
    if (timerEvents) {
        timerEvents->release();
        timerEvents = 0;
    }

    if (timerEventSource) {
        if (workLoop) {
            workLoop->removeEventSource(timerEventSource);
        }
        
        timerEventSource->release();
        timerEventSource = NULL;
    }

	if (reserved->idleTimer) {
		if (workLoop) {
			workLoop->removeEventSource(reserved->idleTimer);
		}

		reserved->idleTimer->release();
		reserved->idleTimer = NULL;
	}

    if (commandGate) {
        if (workLoop) {
            workLoop->removeEventSource(commandGate);
        }
        
        commandGate->release();
        commandGate = NULL;
    }

    if (workLoop) {
        workLoop->release();
        workLoop = NULL;
    }

	if (reserved) {
		IOFree (reserved, sizeof(struct ExpansionData));
	}
    
    super::free();
}

bool IOAudioDevice::initHardware(IOService *provider)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::initHardware(%p)\n", this, provider);
#endif

    return true;
}

bool IOAudioDevice::start(IOService *provider)
{
    static IOPMPowerState powerStates[2] = {
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {1, IOPMDeviceUsable, IOPMPowerOn, IOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
    };
    
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::start(%p)\n", this, provider);
#endif

    if (!super::start(provider)) {
        return false;
    }

	if (provider->getProperty("preserveIODeviceTree") != 0) {		// [3206968]
		provider->callPlatformFunction("mac-io-publishChildren", 0, (void*)this, (void*)0, (void*)0, (void*)0);
	}

    assert(workLoop);
    
    commandGate = IOCommandGate::commandGate(this);
    if (!commandGate) {
        return false;
    }
    
    workLoop->addEventSource(commandGate);

    if (!initHardware(provider)) {
        return false;
    }

    if (familyManagePower) {
        PMinit();
        provider->joinPMtree(this);
        
        if (pm_vars != NULL) {
            duringStartup = true;
            registerPowerDriver(this, powerStates, NUM_POWER_STATES);
            changePowerStateTo(1);
            duringStartup = false;
        }
    }

    registerService();

    return true;
}

void IOAudioDevice::stop(IOService *provider)
{    
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::stop(%p)\n", this, provider);
#endif
    
    if (timerEventSource) {
        if (workLoop) {
            workLoop->removeEventSource(timerEventSource);
        }
        
        timerEventSource->release();
        timerEventSource = NULL;
    }

    if (reserved->idleTimer) {
        if (workLoop) {
            workLoop->removeEventSource(reserved->idleTimer);
        }

        reserved->idleTimer->release();
        reserved->idleTimer = NULL;
    }

    removeAllTimerEvents();

    deactivateAllAudioEngines();
    detachAllAudioPorts();

    if (familyManagePower) {
        PMstop();
    }
    
    if (commandGate) {
        if (workLoop) {
            workLoop->removeEventSource(commandGate);
        }
        
        commandGate->release();
        commandGate = NULL;
    }

    super::stop(provider);
}

bool IOAudioDevice::willTerminate(IOService *provider, IOOptionBits options)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::willTerminate(%p, %lx)\n", this, provider, options);
#endif

    OSCollectionIterator *engineIterator;
    
    engineIterator = OSCollectionIterator::withCollection(audioEngines);
    if (engineIterator) {
        IOAudioEngine *audioEngine;
        
        while (audioEngine = OSDynamicCast(IOAudioEngine, engineIterator->getNextObject())) {
            audioEngine->setState(kIOAudioEngineStopped);
        }
        engineIterator->release();
    }

	return super::willTerminate(provider, options);
}

void IOAudioDevice::setFamilyManagePower(bool manage)
{
    familyManagePower = manage;
}

IOReturn IOAudioDevice::setPowerState(unsigned long powerStateOrdinal, IOService *device)
{
    IOReturn result = IOPMCannotRaisePower;
    
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::setPowerState(%lu, %p)\n", this, powerStateOrdinal, device);
#endif
    
    if (!duringStartup) {
        if (powerStateOrdinal >= NUM_POWER_STATES) {
            result = IOPMNoSuchState;
        } else {
            IOCommandGate *cg;
            
            cg = getCommandGate();
            
            if (cg) {
                result = cg->runAction(setPowerStateAction, (void *)powerStateOrdinal, (void *)device);
            }
        }
    }
    
    return result;
}

IOReturn IOAudioDevice::setPowerStateAction(OSObject *owner, void *arg1, void *arg2, void *arg3, void *arg4)
{
    IOReturn result = kIOReturnBadArgument;
    
    if (owner) {
        IOAudioDevice *audioDevice = OSDynamicCast(IOAudioDevice, owner);
        
        if (audioDevice) {
            result = audioDevice->protectedSetPowerState((unsigned long)arg1, (IOService *)arg2);
        }
    }
    
    return result;
}

IOReturn IOAudioDevice::protectedSetPowerState(unsigned long powerStateOrdinal, IOService *device)
{
    IOReturn result = IOPMAckImplied;

#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::protectedSetPowerState(%lu, %p)\n", this, powerStateOrdinal, device);
#endif
    
    if (asyncPowerStateChangeInProgress) {
        waitForPendingPowerStateChange();
    }
    
    if (powerStateOrdinal == 0) {	// Sleep
        if (getPowerState() != kIOAudioDeviceSleep) {
            pendingPowerState = kIOAudioDeviceSleep;

            // Stop all audio engines
            if (audioEngines && (numRunningAudioEngines > 0)) {
                OSCollectionIterator *audioEngineIterator;
                
                audioEngineIterator = OSCollectionIterator::withCollection(audioEngines);
                
                if (audioEngineIterator) {
                    IOAudioEngine *audioEngine;
                    
                    while (audioEngine = (IOAudioEngine *)audioEngineIterator->getNextObject()) {
                        if (audioEngine->getState() == kIOAudioEngineRunning) {
                            audioEngine->pauseAudioEngine();
                        }
                    }
                    
                    audioEngineIterator->release();
                }
            }
        }
    } else if (powerStateOrdinal == 1) {	// Wake
        if (getPowerState() == kIOAudioDeviceSleep) {	// Need to change state if sleeping
            if (numRunningAudioEngines == 0) {
                pendingPowerState = kIOAudioDeviceIdle;
            } else {
                pendingPowerState = kIOAudioDeviceActive;
            }
        }
    }
    
    if (currentPowerState != pendingPowerState) {
        UInt32 microsecondsUntilComplete = 0;
        
        result = initiatePowerStateChange(&microsecondsUntilComplete);
        if (result == kIOReturnSuccess) {
            result = microsecondsUntilComplete;
        }
    }
    
    return result;
}

void IOAudioDevice::waitForPendingPowerStateChange()
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::waitForPendingPowerStateChange()\n", this);
#endif

    if (asyncPowerStateChangeInProgress) {
        IOCommandGate *cg;
        
        cg = getCommandGate();
        
        if (cg) {
            cg->commandSleep((void *)&asyncPowerStateChangeInProgress);
            assert(!asyncPowerStateChangeInProgress);
        } else {
            IOLog("IOAudioDevice[%p]::waitForPendingPowerStateChange() - internal error - unable to get the command gate.\n", this);
            return;
        }
    }
}

IOReturn IOAudioDevice::initiatePowerStateChange(UInt32 *microsecondsUntilComplete)
{
    IOReturn result = kIOReturnSuccess;

#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::initiatePowerStateChange(%p) - current = %d - pending = %d\n", this, microsecondsUntilComplete, currentPowerState, pendingPowerState);
#endif
    
    if (currentPowerState != pendingPowerState) {
        UInt32 localMicsUntilComplete, *micsUntilComplete = NULL;
        
        if (microsecondsUntilComplete != NULL) {
            micsUntilComplete = microsecondsUntilComplete;
        } else {
            micsUntilComplete = &localMicsUntilComplete;
        }
        
        *micsUntilComplete = 0;
        
        asyncPowerStateChangeInProgress = true;
        
        result = performPowerStateChange(currentPowerState, pendingPowerState, micsUntilComplete);
        
        if (result == kIOReturnSuccess) {
            if (*micsUntilComplete == 0) {
                asyncPowerStateChangeInProgress = false;
                protectedCompletePowerStateChange();
            }
        } else {
            asyncPowerStateChangeInProgress = false;
        }
    }
    
    return result;
}

IOReturn IOAudioDevice::completePowerStateChange()
{
    IOReturn result = kIOReturnError;
    IOCommandGate *cg;
    
    cg = getCommandGate();
    
    if (cg) {
        result = cg->runAction(completePowerStateChangeAction);
    }
    
    return result;
}

IOReturn IOAudioDevice::completePowerStateChangeAction(OSObject *owner, void *arg1, void *arg2, void *arg3, void *arg4)
{
    IOReturn result = kIOReturnBadArgument;
    
    if (owner) {
        IOAudioDevice *audioDevice = OSDynamicCast(IOAudioDevice, owner);
        
        if (audioDevice) {
            result = audioDevice->protectedCompletePowerStateChange();
        }
    }
    
    return result;
}

IOReturn IOAudioDevice::protectedCompletePowerStateChange()
{
    IOReturn result = kIOReturnSuccess;

#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::protectedCompletePowerStateChange() - current = %d - pending = %d\n", this, currentPowerState, pendingPowerState);
#endif

    if (currentPowerState != pendingPowerState) {
		IOCommandGate *cg;

		cg = getCommandGate();
        // If we're waking, we fire off the timers and resync them
        // Then restart the audio engines that were running before the sleep
        if (currentPowerState == kIOAudioDeviceSleep) {	
            clock_get_uptime(&previousTimerFire);
            SUB_ABSOLUTETIME(&previousTimerFire, &minimumInterval);
            
            if (timerEvents && (timerEvents->getCount() > 0)) {
                dispatchTimerEvents(true);
            }
            
            if (audioEngines && (numRunningAudioEngines > 0)) {
                OSCollectionIterator *audioEngineIterator;
                
                audioEngineIterator = OSCollectionIterator::withCollection(audioEngines);
                
                if (audioEngineIterator) {
                    IOAudioEngine *audioEngine;
                    
                    while (audioEngine = (IOAudioEngine *)audioEngineIterator->getNextObject()) {
                        if (audioEngine->getState() == kIOAudioEnginePaused) {
                            audioEngine->resumeAudioEngine();
                        }
                    }
                        
                    audioEngineIterator->release();
                }
            }
        }
    
        if (asyncPowerStateChangeInProgress) {
            acknowledgePowerChange(this);
            asyncPowerStateChangeInProgress = false;
        
            if (cg) {
                cg->commandWakeup((void *)&asyncPowerStateChangeInProgress);
            }
        }
        
        currentPowerState = pendingPowerState;
		
		if (cg) {
			cg->commandWakeup(&currentPowerState);
		}
    }
    
    return result;
}

IOReturn IOAudioDevice::performPowerStateChange(IOAudioDevicePowerState oldPowerState,
                                                IOAudioDevicePowerState newPowerState,
                                                UInt32 *microsecondsUntilComplete)
{
    return kIOReturnSuccess;
}

IOAudioDevicePowerState IOAudioDevice::getPowerState()
{
    return currentPowerState;
}

IOAudioDevicePowerState IOAudioDevice::getPendingPowerState()
{
    return pendingPowerState;
}

void IOAudioDevice::audioEngineStarting()
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::audioEngineStarting() - numRunningAudioEngines = %ld\n", this, numRunningAudioEngines + 1);
#endif

    numRunningAudioEngines++;
    
    if (numRunningAudioEngines == 1) {	// First audio engine starting - need to be in active state
        if (getPowerState() == kIOAudioDeviceIdle) {	// Go active
            if (asyncPowerStateChangeInProgress) {	// Sleep if there is a transition in progress
                waitForPendingPowerStateChange();
            }
            
            pendingPowerState = kIOAudioDeviceActive;
            
            initiatePowerStateChange();

            if (asyncPowerStateChangeInProgress) {	// Sleep if there is a transition in progress
                waitForPendingPowerStateChange();
            }
        } else if (getPendingPowerState () != kIOAudioDeviceSleep) {
			// Make sure that when the idle timer fires that we won't go to sleep.
            pendingPowerState = kIOAudioDeviceActive;
		}
    }
}

void IOAudioDevice::audioEngineStopped()
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::audioEngineStopped() - numRunningAudioEngines = %ld\n", this, numRunningAudioEngines - 1);
#endif

    numRunningAudioEngines--;
    
    if (numRunningAudioEngines == 0) {	// Last audio engine stopping - need to be idle
        if (getPowerState() == kIOAudioDeviceActive) {	// Go idle
            if (asyncPowerStateChangeInProgress) {	// Sleep if there is a transition in progress
                waitForPendingPowerStateChange();
            }
            
            pendingPowerState = kIOAudioDeviceIdle;

			scheduleIdleAudioSleep();
        }
    }
}

IOWorkLoop *IOAudioDevice::getWorkLoop() const
{
    return workLoop;
}

IOCommandGate *IOAudioDevice::getCommandGate() const
{
    return commandGate;
}

void IOAudioDevice::setDeviceName(const char *deviceName)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::setDeviceName(%p)\n", this, deviceName);
#endif

    if (deviceName) {
        setProperty(kIOAudioDeviceNameKey, deviceName);
    }
}

void IOAudioDevice::setDeviceShortName(const char *shortName)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::setDeviceName(%p)\n", this, shortName);
#endif

    if (shortName) {
        setProperty(kIOAudioDeviceShortNameKey, shortName);
    }
}

void IOAudioDevice::setManufacturerName(const char *manufacturerName)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::setManufacturerName(%p)\n", this, manufacturerName);
#endif

    if (manufacturerName) {
        setProperty(kIOAudioDeviceManufacturerNameKey, manufacturerName);
    }
}

IOReturn IOAudioDevice::activateAudioEngine(IOAudioEngine *audioEngine)
{
    return activateAudioEngine(audioEngine, true);
}

IOReturn IOAudioDevice::activateAudioEngine(IOAudioEngine *audioEngine, bool shouldStartAudioEngine)
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::activateAudioEngine(%p, %d)\n", this, audioEngine, shouldStartAudioEngine);
#endif

    if (!audioEngine || !audioEngines) {
        return kIOReturnBadArgument;
    }

    if (!audioEngine->attach(this)) {
        return kIOReturnError;
    }

    if (shouldStartAudioEngine) {
        if (!audioEngine->start(this)) {
            audioEngine->detach(this);
            return kIOReturnError;
        }
    }

    audioEngine->deviceStartedAudioEngine = shouldStartAudioEngine;

    audioEngines->setObject(audioEngine);
    audioEngine->setIndex(audioEngines->getCount()-1);
    
    audioEngine->registerService();
    
    return kIOReturnSuccess;
}

void IOAudioDevice::deactivateAllAudioEngines()
{
    OSCollectionIterator *engineIterator;
    
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::deactivateAllAudioEngines()\n", this);
#endif

    if (!audioEngines) {
        return;
    }

    engineIterator = OSCollectionIterator::withCollection(audioEngines);
    if (engineIterator) {
        IOAudioEngine *audioEngine;
        
        while (audioEngine = OSDynamicCast(IOAudioEngine, engineIterator->getNextObject())) {
            audioEngine->stopAudioEngine();
            if (!isInactive()) {
                audioEngine->terminate();
            }
        }
        engineIterator->release();
    }

    audioEngines->flushCollection();
}

IOReturn IOAudioDevice::attachAudioPort(IOAudioPort *port, IORegistryEntry *parent, IORegistryEntry *child)
{
    if (!port || !audioPorts) {
        return kIOReturnBadArgument;
    }

    if (!port->attach(this)) {
        return kIOReturnError;
    }

    if (!port->start(this)) {
        port->detach(this);
        return kIOReturnError;
    }

    audioPorts->setObject(port);

    port->registerService();

    if (!parent) {
        parent = getRegistryRoot();
    }
    port->attachToParent(parent, gIOAudioPlane);

    if (child) {
        child->attachToParent(port, gIOAudioPlane);
    }

    return kIOReturnSuccess;
}

void IOAudioDevice::detachAllAudioPorts()
{
    OSCollectionIterator *iterator;
    
    if (!audioPorts) {
        return;
    }

    iterator = OSCollectionIterator::withCollection(audioPorts);

    if (iterator) {
        IOAudioPort *port;
        
        while (port = (IOAudioPort *)iterator->getNextObject()) {
            if (!isInactive()) {
                port->terminate();
            }
            port->detachAll(gIOAudioPlane);
        }
        
        iterator->release();
    }
    
    audioPorts->flushCollection();
}

void IOAudioDevice::flushAudioControls()
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::flushAudioControls()\n", this);
#endif

    if (audioPorts) {
        OSCollectionIterator *portIterator;

        portIterator = OSCollectionIterator::withCollection(audioPorts);
        if (portIterator) {
            IOAudioPort *audioPort;

            while (audioPort = (IOAudioPort *)portIterator->getNextObject()) {
                if (OSDynamicCast(IOAudioPort, audioPort)) {
                    if (audioPort->audioControls) {
                        OSCollectionIterator *controlIterator;

                        controlIterator = OSCollectionIterator::withCollection(audioPort->audioControls);

                        if (controlIterator) {
                            IOAudioControl *audioControl;

                            while (audioControl = (IOAudioControl *)controlIterator->getNextObject()) {
                                audioControl->flushValue();
                            }
                            controlIterator->release();
                        }
                    }
                }
            }
            portIterator->release();
        }
    }
    
    // This code will flush controls attached to an IOAudioPort and a default on a audio engine
    // more than once
    // We need to update this to create a single master list of controls and use that to flush
    // each only once
    if (audioEngines) {
        OSCollectionIterator *audioEngineIterator;
        
        audioEngineIterator = OSCollectionIterator::withCollection(audioEngines);
        if (audioEngineIterator) {
            IOAudioEngine *audioEngine;
            
            while (audioEngine = (IOAudioEngine *)audioEngineIterator->getNextObject()) {
                if (audioEngine->defaultAudioControls) {
                    OSCollectionIterator *controlIterator;
                    
                    controlIterator = OSCollectionIterator::withCollection(audioEngine->defaultAudioControls);
                    if (controlIterator) {
                        IOAudioControl *audioControl;
                        
                        while (audioControl = (IOAudioControl *)controlIterator->getNextObject()) {
                            audioControl->flushValue();
                        }
                        controlIterator->release();
                    }
                }
            }
            
            audioEngineIterator->release();
        }
    }
}

IOReturn IOAudioDevice::addTimerEvent(OSObject *target, TimerEvent event, AbsoluteTime interval)
{
    IOReturn result = kIOReturnSuccess;
    IOAudioTimerEvent *newEvent;
    
#ifdef DEBUG_CALLS
    UInt64 newInt;
    absolutetime_to_nanoseconds(interval, &newInt);
    IOLog("IOAudioDevice::addTimerEvent(%p, %p, %lums)\n", target, event, (UInt32)(newInt/1000000));
#endif

    if (!event) {
        return kIOReturnBadArgument;
    }

    newEvent = new IOAudioTimerEvent;
    newEvent->target = target;
    newEvent->event = event;
    newEvent->interval = interval;

    if (!timerEvents) {
        IOWorkLoop *wl;

        timerEvents = OSDictionary::withObjects((const OSObject **)&newEvent, (const OSSymbol **)&target, 1, 1);
        
        timerEventSource = IOTimerEventSource::timerEventSource(this, timerFired);
        wl = getWorkLoop();
        if (!timerEventSource || !wl || (wl->addEventSource(timerEventSource) != kIOReturnSuccess)) {
            return kIOReturnError;
        }
        timerEventSource->enable();
    } else {
        timerEvents->setObject((OSSymbol *)target, newEvent);
    }

    newEvent->release();
    
    assert(timerEvents);

    if (timerEvents->getCount() == 1) {
        AbsoluteTime nextTimerFire;
        
        minimumInterval = interval;

        assert(timerEventSource);

        clock_get_uptime(&previousTimerFire);
        
        nextTimerFire = previousTimerFire;
        ADD_ABSOLUTETIME(&nextTimerFire, &minimumInterval);
        
        result = timerEventSource->wakeAtTime(nextTimerFire);
        
#ifdef DEBUG_TIMER
        {
            UInt64 nanos;
            absolutetime_to_nanoseconds(minimumInterval, &nanos);
            IOLog("IOAudioDevice::addTimerEvent() - scheduling timer to fire in %lums - previousTimerFire = {%ld,%lu}\n", (UInt32) (nanos / 1000000), previousTimerFire.hi, previousTimerFire.lo);
        }
#endif

        if (result != kIOReturnSuccess) {
            IOLog("IOAudioDevice::addTimerEvent() - error 0x%x setting timer wake time - timer events will be disabled.\n", result);
        }
    } else if (CMP_ABSOLUTETIME(&interval, &minimumInterval) < 0) {
        AbsoluteTime currentNextFire, desiredNextFire;
        
        clock_get_uptime(&desiredNextFire);
        ADD_ABSOLUTETIME(&desiredNextFire, &interval);

        currentNextFire = previousTimerFire;
        ADD_ABSOLUTETIME(&currentNextFire, &minimumInterval);
        
        minimumInterval = interval;

        if (CMP_ABSOLUTETIME(&desiredNextFire, &currentNextFire) < 0) {
            assert(timerEventSource);
            
#ifdef DEBUG_TIMER
            {
                UInt64 nanos;
                absolutetime_to_nanoseconds(interval, &nanos);
                IOLog("IOAudioDevice::addTimerEvent() - scheduling timer to fire in %lums at {%ld,%lu} - previousTimerFire = {%ld,%lu}\n", (UInt32) (nanos / 1000000), desiredNextFire.hi, desiredNextFire.lo, previousTimerFire.hi, previousTimerFire.lo);
            }
#endif

            result = timerEventSource->wakeAtTime(desiredNextFire);
            if (result != kIOReturnSuccess) {
                IOLog("IOAudioDevice::addTimerEvent() - error 0x%x setting timer wake time - timer events will be disabled.\n", result);
            }
        }
    }
    
    return result;
}

void IOAudioDevice::removeTimerEvent(OSObject *target)
{
    IOAudioTimerEvent *removedTimerEvent;
    
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice::removeTimerEvent(%p)\n", target);
#endif
    
    if (!timerEvents) {
        return;
    }

    removedTimerEvent = (IOAudioTimerEvent *)timerEvents->getObject((const OSSymbol *)target);
    if (removedTimerEvent) {
        removedTimerEvent->retain();
        timerEvents->removeObject((const OSSymbol *)target);
        if (timerEvents->getCount() == 0) {
            assert(timerEventSource);
            timerEventSource->cancelTimeout();
        } else if (CMP_ABSOLUTETIME(&removedTimerEvent->interval, &minimumInterval) <= 0) { // Need to find a new minimum interval
            OSCollectionIterator *iterator;
            IOAudioTimerEvent *timerEvent;
            AbsoluteTime nextTimerFire;
            OSSymbol *obj;

            iterator = OSCollectionIterator::withCollection(timerEvents);
            
            if (iterator) {
                obj = (OSSymbol *)iterator->getNextObject();
                timerEvent = (IOAudioTimerEvent *)timerEvents->getObject(obj);
    
                if (timerEvent) {
                    minimumInterval = timerEvent->interval;
    
                    while ((obj = (OSSymbol *)iterator->getNextObject()) && (timerEvent = (IOAudioTimerEvent *)timerEvents->getObject(obj))) {
                        if (CMP_ABSOLUTETIME(&timerEvent->interval, &minimumInterval) < 0) {
                            minimumInterval = timerEvent->interval;
                        }
                    }
                }
    
                iterator->release();
            }

            assert(timerEventSource);

            nextTimerFire = previousTimerFire;
            ADD_ABSOLUTETIME(&nextTimerFire, &minimumInterval);
            
#ifdef DEBUG_TIMER
            {
                AbsoluteTime now, then;
                UInt64 nanos, mi;
                clock_get_uptime(&now);
                then = nextTimerFire;
                absolutetime_to_nanoseconds(minimumInterval, &mi);
                if (CMP_ABSOLUTETIME(&then, &now)) {
                    SUB_ABSOLUTETIME(&then, &now);
                    absolutetime_to_nanoseconds(then, &nanos);
                    IOLog("IOAudioDevice::removeTimerEvent() - scheduling timer to fire in %lums at {%ld,%lu} - previousTimerFire = {%ld,%lu} - interval=%lums\n", (UInt32) (nanos / 1000000), nextTimerFire.hi, nextTimerFire.lo, previousTimerFire.hi, previousTimerFire.lo, (UInt32)(mi/1000000));
                } else {
                    SUB_ABSOLUTETIME(&now, &then);
                    absolutetime_to_nanoseconds(now, &nanos);
                    IOLog("IOAudioDevice::removeTimerEvent() - scheduling timer to fire in -%lums - previousTimerFire = {%ld,%lu}\n", (UInt32) (nanos / 1000000), previousTimerFire.hi, previousTimerFire.lo);
                }
            }
#endif

            timerEventSource->wakeAtTime(nextTimerFire);
        }

        removedTimerEvent->release();
    }
}

void IOAudioDevice::removeAllTimerEvents()
{
#ifdef DEBUG_CALLS
    IOLog("IOAudioDevice[%p]::removeAllTimerEvents()\n", this);
#endif

    if (timerEventSource) {
        timerEventSource->cancelTimeout();
    }
    
    if (timerEvents) {
        timerEvents->flushCollection();
    }
}

void IOAudioDevice::timerFired(OSObject *target, IOTimerEventSource *sender)
{
    if (target) {
        IOAudioDevice *audioDevice = OSDynamicCast(IOAudioDevice, target);
        
        if (audioDevice) {
            audioDevice->dispatchTimerEvents(false);
        }
    }
}

void IOAudioDevice::dispatchTimerEvents(bool force)
{
    if (timerEvents) {
#ifdef DEBUG_TIMER
        AbsoluteTime now, delta;
        UInt64 nanos;
        
        clock_get_uptime(&now);
        delta = now;
        SUB_ABSOLUTETIME(&delta, &previousTimerFire);
        absolutetime_to_nanoseconds(delta, &nanos);
        IOLog("IOAudioDevice::dispatchTimerEvents() - woke up %lums after last fire - now = {%ld,%lu} - previousFire = {%ld,%lu}\n", (UInt32)(nanos / 1000000), now.hi, now.lo, previousTimerFire.hi, previousTimerFire.lo);
#endif

        if (force || (getPowerState() != kIOAudioDeviceSleep)) {
            OSIterator *iterator;
            OSSymbol *target;
            AbsoluteTime nextTimerFire, currentInterval;
            
            currentInterval = minimumInterval;
        
            assert(timerEvents);
        
            iterator = OSCollectionIterator::withCollection(timerEvents);
        
            if (iterator) {
                while (target = (OSSymbol *)iterator->getNextObject()) {
                    IOAudioTimerEvent *timerEvent;
                    timerEvent = (IOAudioTimerEvent *)timerEvents->getObject(target);
        
                    if (timerEvent) {
                        (*timerEvent->event)(timerEvent->target, this);
                    }
                }
        
                iterator->release();
            }
        
            if (timerEvents->getCount() > 0) {
                ADD_ABSOLUTETIME(&previousTimerFire, &currentInterval);
                nextTimerFire = previousTimerFire;
                ADD_ABSOLUTETIME(&nextTimerFire, &minimumInterval);
        
                assert(timerEventSource);
                
#ifdef DEBUG_TIMER
                {
                    AbsoluteTime later;
                    UInt64 mi;
                    later = nextTimerFire;
                    absolutetime_to_nanoseconds(minimumInterval, &mi);
                    if (CMP_ABSOLUTETIME(&later, &now)) {
                        SUB_ABSOLUTETIME(&later, &now);
                        absolutetime_to_nanoseconds(later, &nanos);
                        IOLog("IOAudioDevice::dispatchTimerEvents() - scheduling timer to fire in %lums at {%ld,%lu} - previousTimerFire = {%ld,%lu} - interval=%lums\n", (UInt32) (nanos / 1000000), nextTimerFire.hi, nextTimerFire.lo, previousTimerFire.hi, previousTimerFire.lo, (UInt32)(mi/1000000));
                    } else {
                        SUB_ABSOLUTETIME(&now, &later);
                        absolutetime_to_nanoseconds(now, &nanos);
                        IOLog("IOAudioDevice::dispatchTimerEvents() - scheduling timer to fire in -%lums - previousTimerFire = {%ld,%lu}\n", (UInt32) (nanos / 1000000), previousTimerFire.hi, previousTimerFire.lo);
                    }
                }
#endif
    
                timerEventSource->wakeAtTime(nextTimerFire);
            }
        }
    }
}

