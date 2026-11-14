// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once

#include <IOKit/IOTypes.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>

struct IOStorageAttributes {
    UInt64 flags;
};

typedef void (*IOStorageCompletionAction)(void *target, void *parameter, IOReturn status, UInt64 actualByteCount);

struct IOStorageCompletion {
    void *target;
    IOStorageCompletionAction action;
    void *parameter;
};

#define kIOBlockStorageDeviceClass "IOBlockStorageDevice"
#define kIOBlockStorageDeviceTypeKey "device-type"
#define kIOBlockStorageDeviceTypeGeneric "Generic"
#define kIOBlockStorageDeviceWriteCacheStateKey "WriteCacheState"

class IOBlockStorageDevice : public IOService {
    OSDeclareAbstractStructors(IOBlockStorageDevice)

public:
    virtual bool init(OSDictionary *properties = nullptr) override {
        if (!IOService::init(properties)) return false;
        setProperty(kIOBlockStorageDeviceTypeKey, kIOBlockStorageDeviceTypeGeneric);
        return true;
    }

    virtual bool start(IOService *provider) override {
        return IOService::start(provider);
    }

    virtual void free() override {
        IOService::free();
    }

    virtual IOReturn doEjectMedia() = 0;
    virtual IOReturn doFormatMedia(UInt64 byteCapacity) = 0;
    virtual UInt32   doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const = 0;
    virtual IOReturn doLockUnlockMedia(bool doLock) { (void)doLock; return kIOReturnUnsupported; }
    virtual IOReturn doSynchronizeCache() = 0;
    virtual char *   getAdditionalDeviceInfoString() { return nullptr; }
    virtual char *   getProductString() = 0;
    virtual char *   getRevisionString() = 0;
    virtual char *   getVendorString() = 0;
    virtual IOReturn reportBlockSize(UInt64 *blockSize) = 0;
    virtual IOReturn reportEjectability(bool *isEjectable) = 0;
    virtual IOReturn reportLockability(bool *isLockable) { *isLockable = false; return kIOReturnSuccess; }
    virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock) = 0;
    virtual IOReturn reportMediaState(bool *mediaPresent, bool *mediaChanged) = 0;
    virtual IOReturn reportPollRequirements(bool *pollRequired, bool *pollIsExpensive) {
        *pollRequired = false;
        if (pollIsExpensive) *pollIsExpensive = false;
        return kIOReturnSuccess;
    }
    virtual IOReturn reportRemovability(bool *isRemovable) = 0;
    virtual IOReturn reportWriteProtection(bool *isWriteProtected) = 0;
    virtual IOReturn getWriteCacheState(bool *enabled) = 0;
    virtual IOReturn setWriteCacheState(bool enabled) { (void)enabled; return kIOReturnUnsupported; }
    virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion) = 0;
    virtual IOReturn doSynchronize(UInt64 block, UInt64 nblks, IOOptionBits options) {
        (void)block; (void)nblks; (void)options;
        return doSynchronizeCache();
    }
};
