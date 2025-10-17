//
// SCSI Miniport Driver for Windows 2000
//

#include "nvme2k.h"
#include "utils.h"

//
// DriverEntry - Main entry point
//
ULONG DriverEntry(IN PVOID DriverObject, IN PVOID Argument2)
{
    HW_INITIALIZATION_DATA hwInitData;
    ULONG status;
#if (_WIN32_WINNT < 0x500)
    ULONG HwContext[2];
#endif
    // Break into debugger if attached

#ifdef NVME2K_DBG
    //__asm { int 3 }
    ScsiDebugPrint(0, "nvme2k: DriverEntry called\n");
#endif

    // Zero out the initialization data structure
    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

    // Set size of structure
    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    // Set entry points
    hwInitData.HwInitialize = HwInitialize;
    hwInitData.HwStartIo = HwStartIo;
    hwInitData.HwInterrupt = HwInterrupt;
    hwInitData.HwFindAdapter = HwFindAdapter;
    hwInitData.HwResetBus = HwResetBus;
#if (_WIN32_WINNT >= 0x500)
    hwInitData.HwAdapterControl = HwAdapterControl;
#else
    hwInitData.HwAdapterState = HwAdapterState;
#endif

    // Set driver-specific parameters
    hwInitData.AdapterInterfaceType = PCIBus;  // Change as needed
    hwInitData.DeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    hwInitData.SpecificLuExtensionSize = 0;
    hwInitData.SrbExtensionSize = sizeof(NVME_SRB_EXTENSION);  // Required for PRP list tracking
    hwInitData.NumberOfAccessRanges = 1;
    hwInitData.MapBuffers = TRUE;
    hwInitData.NeedPhysicalAddresses = TRUE;
    hwInitData.TaggedQueuing = TRUE;
    hwInitData.AutoRequestSense = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;

    // Vendor/Device IDs (for PCI devices)
    hwInitData.VendorIdLength = 0;
    hwInitData.VendorId = NULL;
    hwInitData.DeviceIdLength = 0;
    hwInitData.DeviceId = NULL;

#if (_WIN32_WINNT < 0x500)
    HwContext[0] = 0;
    HwContext[1] = 0;
#endif
    // Call port driver
    status = ScsiPortInitialize(DriverObject, Argument2,
                                &hwInitData, 
#if (_WIN32_WINNT >= 0x500)
                                NULL
#else
                                HwContext
#endif
                            );

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: DriverEntry exiting with status 0x%08X\n", status);
#endif
    return status;
}

ULONG HwFoundAdapter(
    IN PHW_DEVICE_EXTENSION DevExt,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN PUCHAR pciBuffer)
{
    PACCESS_RANGE accessRange;
    ULONG bar0;
    ULONG barSize;
    ULONG tempSize;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - found NVMe device VID=%04X DID=%04X at bus %d slot %d\n",
                   DevExt->VendorId, DevExt->DeviceId, DevExt->BusNumber, DevExt->SlotNumber);
#endif

    // Read subsystem IDs using helper function
    DevExt->SubsystemVendorId = ReadPciConfigWord(DevExt, PCI_SUBSYSTEM_VENDOR_ID_OFFSET);
    DevExt->SubsystemId = ReadPciConfigWord(DevExt, PCI_SUBSYSTEM_ID_OFFSET);

    // Enable PCI device (Bus Master, Memory Space)
    WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET,
                      PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE);

    // Read interrupt configuration from PCI config space
    // This is probably redundant on Win2K and can be ifdefed out
    {
        UCHAR interruptLine = ReadPciConfigByte(DevExt, PCI_INTERRUPT_LINE_OFFSET);
        UCHAR interruptPin = ReadPciConfigByte(DevExt, PCI_INTERRUPT_PIN_OFFSET);

#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - PCI Interrupt Line=%d Pin=%d\n",
                       interruptLine, interruptPin);
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - ConfigInfo BEFORE: BusInterruptLevel=%d Vector=%d Mode=%d\n",
                       ConfigInfo->BusInterruptLevel, ConfigInfo->BusInterruptVector, ConfigInfo->InterruptMode);
#endif

        // CRITICAL FOR NT4: When manually setting SystemIoBusNumber/SlotNumber,
        // SCSI port doesn't automatically query interrupt configuration from HAL.
        // We MUST set valid interrupt parameters or we'll get STATUS_INVALID_PARAMETER.
        // For PCI: BusInterruptLevel = interrupt line, mode = LevelSensitive
        if (interruptPin != 0 && interruptLine != 0 && interruptLine != 0xFF) {
            ConfigInfo->BusInterruptLevel = interruptLine;
            ConfigInfo->BusInterruptVector = interruptLine;
            ConfigInfo->InterruptMode = LevelSensitive;
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - Set interrupt from PCI config: Level=%d\n", interruptLine);
#endif
        } else {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - WARNING: No valid PCI interrupt configured\n");
#endif
            // If no valid interrupt, still need to set something valid for NT4
            // Use polling mode - but this might not work on NT4!
            ConfigInfo->BusInterruptLevel = 0;
            ConfigInfo->BusInterruptVector = 0;
        }
    }

    // Set the number of access ranges we're using (1 for BAR0)
    // NT4 may require this to be explicitly set for resource translation
    ConfigInfo->NumberOfAccessRanges = 1;

    // Configure the adapter settings
    // With PRP lists, we can support up to 512 entries per list = 2MB per transfer
    // Being conservative: 512 pages * 4KB = 2MB
    ConfigInfo->MaximumTransferLength = 512 * PAGE_SIZE;  // 2MB with PRP lists
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = FALSE;
    ConfigInfo->AdapterScansDown = FALSE;
    ConfigInfo->Dma32BitAddresses = TRUE;
#if (_WIN32_WINNT >= 0x500)
    ConfigInfo->Dma64BitAddresses = TRUE;  // NVMe supports 64-bit addressing
#endif    
    ConfigInfo->MaximumNumberOfTargets = 2;  // Support TargetId 0 and 1
    ConfigInfo->NumberOfPhysicalBreaks = 511;  // PRP1 + PRP list (512 entries)
    ConfigInfo->AlignmentMask = 0x3;  // DWORD alignment
    ConfigInfo->NeedPhysicalAddresses = TRUE;  // Required for ScsiPortGetPhysicalAddress to work
    ConfigInfo->TaggedQueuing = TRUE;  // Support tagged command queuing
    ConfigInfo->MultipleRequestPerLu = TRUE;  // Allow multiple outstanding commands per LUN
    ConfigInfo->AutoRequestSense = TRUE;  // Automatically provide sense data on errors
    // Note: SrbExtensionSize must be set in HW_INITIALIZATION_DATA, not here

#ifdef NVME2K_DBG_EXTRA
    {
        ULONG a;
        accessRange = &((*(ConfigInfo->AccessRanges))[0]);
        for (a = 0; a < ConfigInfo->NumberOfAccessRanges; a++) {
            ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - range:%u start:%08X, length:%08X, inMemory:%d\n",
                           a, (ULONG)(accessRange->RangeStart.QuadPart),
                           accessRange->RangeLength,
                           accessRange->RangeInMemory);
            accessRange++;
        }
    }
#endif
    // Again, most of this BAR dance has been already done by Win2k PnP
    // We can probably ifdef it out on Win2k
    // Get BAR0 (Controller registers for NVMe)
    accessRange = &((*(ConfigInfo->AccessRanges))[0]);
    accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(0);
    accessRange->RangeLength = 0;

    bar0 = ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_0);

    if (bar0 & 0x1) {
        // I/O Space (unlikely for NVMe but handle it)
        accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(bar0 & 0xFFFFFFFC);
        accessRange->RangeInMemory = FALSE;
    } else {
        // Memory Space (typical for NVMe)
        accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(bar0 & 0xFFFFFFF0);
        accessRange->RangeInMemory = TRUE;
    }

    WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_0, 0xFFFFFFFF);

    // Read back the modified value
    barSize = ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_0);

    // Restore original BAR value
    WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_0, bar0);

    // Calculate size
    // The size is the bitwise NOT of the value read back (ignoring type bits), plus one.
    if (accessRange->RangeInMemory) {
        accessRange->RangeLength = ~(barSize & 0xFFFFFFF0) + 1;
    } else {
        accessRange->RangeLength = ~(barSize & 0xFFFFFFFC) + 1;
    }

    DevExt->ControllerRegistersLength = accessRange->RangeLength;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - BAR0 base=0x%08X size=0x%08X %s\n",
                   (ULONG)accessRange->RangeStart.QuadPart,
                   accessRange->RangeLength,
                   accessRange->RangeInMemory ? "Memory" : "I/O");
#endif

    // Validate the access range (required for NT4, optional for Win2K+)
    // This checks if the range is available and doesn't conflict with other devices
    if (!ScsiPortValidateRange(
            (PVOID)DevExt,
            ConfigInfo->AdapterInterfaceType,
            ConfigInfo->SystemIoBusNumber,
            accessRange->RangeStart,
            accessRange->RangeLength,
            (BOOLEAN)!accessRange->RangeInMemory)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - ScsiPortValidateRange failed for BAR0\n");
#endif
        return SP_RETURN_ERROR;
    }

    // Map the controller registers
    DevExt->ControllerRegisters = ScsiPortGetDeviceBase(
        (PVOID)DevExt,
        ConfigInfo->AdapterInterfaceType,
        ConfigInfo->SystemIoBusNumber,
        accessRange->RangeStart,
        accessRange->RangeLength,
        (BOOLEAN)!accessRange->RangeInMemory);

    if (DevExt->ControllerRegisters == NULL) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - failed to map controller registers\n");
#endif
        return SP_RETURN_ERROR;
    }
#if 0    
// can be reported in newer OSes
    // Read controller capabilities to determine max queue size
    // This must be done here so we can set ConfigInfo->NumberOfRequests
    {
        ULONGLONG cap;
        USHORT mqes, maxQueueSize;

        cap = NvmeReadReg64(DevExt, NVME_REG_CAP);
        mqes = (USHORT)(cap & NVME_CAP_MQES_MASK);
        maxQueueSize = mqes + 1;  // MQES is 0-based

        // Limit to our maximum (what fits in one page)
        if (maxQueueSize > NVME_MAX_QUEUE_SIZE) {
            maxQueueSize = NVME_MAX_QUEUE_SIZE;
        }

        // Tell SCSI port driver how many outstanding requests we can handle
        // This prevents the OS from sending more requests than we can queue
        ConfigInfo->NumberOfRequests = maxQueueSize;

        ScsiDebugPrint(0, "nvme2k: HwFindAdapter - MQES=%u, setting NumberOfRequests=%u\n",
                       mqes, maxQueueSize);
    }
#endif
    // Allocate uncached memory block
    DevExt->UncachedExtensionSize = UNCACHED_EXTENSION_SIZE;

    DevExt->UncachedExtensionBase = ScsiPortGetUncachedExtension(
        (PVOID)DevExt,
        ConfigInfo,
        DevExt->UncachedExtensionSize);

    if (DevExt->UncachedExtensionBase == NULL) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - failed to allocate uncached memory\n");
#endif
        return SP_RETURN_ERROR;
    }

    // Get physical address of the entire uncached block
    tempSize = DevExt->UncachedExtensionSize;
    DevExt->UncachedExtensionPhys = ScsiPortGetPhysicalAddress(
        (PVOID)DevExt,
        NULL,
        DevExt->UncachedExtensionBase,
        &tempSize);
    
    // Zero out the entire uncached memory
    RtlZeroMemory(DevExt->UncachedExtensionBase, DevExt->UncachedExtensionSize);

    // Initialize allocator
    DevExt->UncachedExtensionOffset = 0;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - allocated %u bytes of uncached memory at virt=%p phys=%08X%08X\n",
                   DevExt->UncachedExtensionSize, DevExt->UncachedExtensionBase,
                   (ULONG)(DevExt->UncachedExtensionPhys.QuadPart >> 32),
                   (ULONG)(DevExt->UncachedExtensionPhys.QuadPart & 0xFFFFFFFF));
#endif

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - success, returning SP_RETURN_FOUND\n");
#endif
    return SP_RETURN_FOUND;
}

//
// HwFindAdapter - Locate and configure the adapter
//
ULONG HwFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    UCHAR pciBuffer[256];
    ULONG slotNumber;
    ULONG busNumber;
    UCHAR baseClass, subClass, progIf;
    ULONG bytesRead;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter:%p called - Bus=%d Slot=%d\n",
                   DeviceExtension, ConfigInfo->SystemIoBusNumber, ConfigInfo->SlotNumber);
#endif

    if (HwContext) {
        busNumber = ((PULONG)HwContext)[0];
        slotNumber = ((PULONG)HwContext)[1];
    } else {
        // Win2k, PnP gives us this directly
        busNumber = ConfigInfo->SystemIoBusNumber;
        slotNumber = ConfigInfo->SlotNumber;
    }
scanloop:
    // Read PCI configuration space for THIS slot only
    bytesRead = ScsiPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        busNumber,
        slotNumber,
        pciBuffer,
        256);

    if (bytesRead == 0) {
        // No device in this slot - tell SCSI port to keep scanning other slots
        goto scannext;
    }

    // Extract Vendor ID and Device ID
    DevExt->VendorId = *(USHORT*)&pciBuffer[PCI_VENDOR_ID_OFFSET];
    DevExt->DeviceId = *(USHORT*)&pciBuffer[PCI_DEVICE_ID_OFFSET];

    // Check for invalid vendor ID
    if (DevExt->VendorId == 0xFFFF || DevExt->VendorId == 0x0000) {
        // No valid device in this slot - tell SCSI port to keep scanning other slots
        goto scannext;
    }

    // Extract class code information
    DevExt->RevisionId = pciBuffer[PCI_REVISION_ID_OFFSET];
    progIf = pciBuffer[PCI_CLASS_CODE_OFFSET];
    subClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 1];
    baseClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 2];

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - bus %d slot %d: VID=%04X DID=%04X Class=%02X%02X%02X\n",
                   busNumber, slotNumber, DevExt->VendorId, DevExt->DeviceId,
                   baseClass, subClass, progIf);
#endif

    // Check if this is an NVMe device
    if (IsNvmeDevice(baseClass, subClass, progIf)) {
        // Found an NVMe device at the slot SCSI port asked us to check!
        DevExt->BusNumber = busNumber;
        DevExt->SlotNumber = slotNumber;
        // Store next slot/bus so we can resume scanning on next call
        if (HwContext) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFindAdapter - NT4: store HwContext to resume scanning\n");
#endif
            ((PULONG)HwContext)[0] = DevExt->BusNumber;
            ((PULONG)HwContext)[1] = DevExt->SlotNumber + 1;
            if (((PULONG)HwContext)[1] >= (PCI_MAX_DEVICES*PCI_MAX_FUNCTION)) {
                ((PULONG)HwContext)[1] = 0;
                ((PULONG)HwContext)[0]++;
            }
        }
#if (_WIN32_WINNT < 0x500)
#ifdef NVME2K_DBG
        if (!HwContext) {
            ScsiDebugPrint(0, "nvme2k: HwFindAdapter - uh oh no HwContext! are we going to scan whole thing again?\n");
        }
#endif
        // NT4: Set Again=TRUE to allow detection of multiple NVMe controllers
        *Again = TRUE;
#else
        // Win2K+: PnP will call us again for each device, no need for Again
        *Again = FALSE;
#endif
        return HwFoundAdapter(DevExt, ConfigInfo, pciBuffer);
    }
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - not an NVMe device\n");
#endif

scannext:
#if (_WIN32_WINNT >= 0x500)
    // Win2K+: PnP gave us a specific device - don't scan if it's not NVMe
    if (HwContext == NULL) {
        *Again = FALSE;
        return SP_RETURN_NOT_FOUND;
    }
#endif
    // NT4: Continue scanning the bus
    slotNumber++;
    if (slotNumber == (PCI_MAX_DEVICES*PCI_MAX_FUNCTION)) {
        busNumber++;
        slotNumber = 0;
        if (busNumber == 16) { // theoretically 256 but we arent going to waste cycles
#if (_WIN32_WINNT < 0x500)
            *Again = FALSE;
#endif
            return SP_RETURN_NOT_FOUND;
        }
    }
    goto scanloop;
}

//
// HwInitialize - Initialize the adapter
//
BOOLEAN HwInitialize(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    ULONG cc, aqa;
    ULONG pageShift;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwInitialize called\n");
#endif
    // Read controller capabilities
    DevExt->ControllerCapabilities = NvmeReadReg64(DevExt, NVME_REG_CAP);
    DevExt->Version = NvmeReadReg32(DevExt, NVME_REG_VS);

    // Parse MQES (Maximum Queue Entries Supported) from CAP register
    // MQES is 0-based, so actual max queue size is MQES + 1
    DevExt->MaxQueueEntries = (USHORT)((DevExt->ControllerCapabilities & NVME_CAP_MQES_MASK) + 1);

    // Calculate doorbell stride (in bytes)
    DevExt->DoorbellStride = 4 << (((ULONG)(DevExt->ControllerCapabilities >> 32) & 0xF));

    // Determine page size (4KB minimum for this driver)
    DevExt->PageSize = PAGE_SIZE; // Hardcoded to 4KB

    //
    // Ensure controller is disabled before configuration.
    // This is a critical step during re-initialization. The controller might
    // take some time to clear its RDY bit after a previous shutdown.
    // We will try a few times to ensure it's disabled.
    //
    {
        int retryCount = 3; // Try up to 3 times
        while (retryCount > 0) {
            cc = NvmeReadReg32(DevExt, NVME_REG_CC);
            cc &= ~NVME_CC_ENABLE;
            NvmeWriteReg32(DevExt, NVME_REG_CC, cc);

            if (NvmeWaitForReady(DevExt, FALSE)) {
                break; // Controller is disabled, proceed.
            }
            retryCount--;
        }
    }

    // Check if the controller successfully became not ready.
    if ((NvmeReadReg32(DevExt, NVME_REG_CSTS) & NVME_CSTS_RDY) != 0) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwInitialize - controller failed to become not ready\n");
#endif
        return FALSE;
    }
    
    // Allocate all uncached memory in proper order to avoid alignment waste
    // Order: All 4KB-aligned buffers first, then smaller aligned buffers
    // This minimizes wasted space from alignment padding

    // Determine actual queue size - use minimum of our max and controller's max
    {
        USHORT queueSize = NVME_MAX_QUEUE_SIZE;
        if (queueSize > DevExt->MaxQueueEntries) {
            queueSize = DevExt->MaxQueueEntries;
        }

        // 1. Allocate Admin SQ (4KB aligned)
        DevExt->AdminQueue.QueueSize = queueSize;
        DevExt->AdminQueue.QueueId = 0;
        if (!AllocateUncachedMemory(DevExt, queueSize * NVME_SQ_ENTRY_SIZE, PAGE_SIZE,
                                    &DevExt->AdminQueue.SubmissionQueue,
                                    &DevExt->AdminQueue.SubmissionQueuePhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate admin SQ\n");
#endif
            return FALSE;
        }

        // 2. Allocate I/O SQ (4KB aligned)
        DevExt->IoQueue.QueueSize = queueSize;
        DevExt->IoQueue.QueueId = 1;
        if (!AllocateUncachedMemory(DevExt, queueSize * NVME_SQ_ENTRY_SIZE, PAGE_SIZE,
                                    &DevExt->IoQueue.SubmissionQueue,
                                    &DevExt->IoQueue.SubmissionQueuePhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate I/O SQ\n");
#endif
            return FALSE;
        }

        // 3. Allocate utility buffer (large enough for SG_LIST_PAGES * 4KB)
        // During init: used for Identify commands
        // After init: repurposed as PRP list page pool
        if (!AllocateUncachedMemory(DevExt, SG_LIST_PAGES * PAGE_SIZE, PAGE_SIZE,
                                    &DevExt->UtilityBuffer,
                                    &DevExt->UtilityBufferPhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate utility buffer\n");
#endif
            return FALSE;
        }
        // Initialize PRP page allocator.
        // It is aliased with UtilityBuffer and only valid after init sequence.
        DevExt->PrpListPages = DevExt->UtilityBuffer;
        DevExt->PrpListPagesPhys = DevExt->UtilityBufferPhys;
        DevExt->PrpListPageBitmap = 0;  // All pages free

        // 4. Allocate Admin CQ (must be page-aligned for NVMe)
        if (!AllocateUncachedMemory(DevExt, queueSize * NVME_CQ_ENTRY_SIZE, PAGE_SIZE,
                                    &DevExt->AdminQueue.CompletionQueue,
                                    &DevExt->AdminQueue.CompletionQueuePhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate admin CQ\n");
#endif
            return FALSE;
        }

        // 5. Allocate I/O CQ (must be page-aligned for NVMe)
        if (!AllocateUncachedMemory(DevExt, queueSize * NVME_CQ_ENTRY_SIZE, PAGE_SIZE,
                                    &DevExt->IoQueue.CompletionQueue,
                                    &DevExt->IoQueue.CompletionQueuePhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate I/O CQ\n");
#endif
            return FALSE;
        }
    }

    // Now all uncached memory is allocated - log final usage
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwInitialize - uncached memory usage: %u / %u bytes\n",
                   DevExt->UncachedExtensionOffset, DevExt->UncachedExtensionSize);
#endif

    // Calculate log2(QueueSize) and mask for both queues (must be power of 2)
    DevExt->AdminQueue.QueueSizeBits = (UCHAR)log2(DevExt->AdminQueue.QueueSize);
    DevExt->AdminQueue.QueueSizeMask = DevExt->AdminQueue.QueueSize - 1;

    DevExt->IoQueue.QueueSizeBits = (UCHAR)log2(DevExt->IoQueue.QueueSize);
    DevExt->IoQueue.QueueSizeMask = DevExt->IoQueue.QueueSize - 1;

    // Initialize SMP synchronization
#ifdef NVME2K_USE_INTERRUPT_LOCK
    DevExt->InterruptLock = 0;
#endif

    // Initialize Admin Queue state
    DevExt->AdminQueue.SubmissionQueueHead = 0;
    DevExt->AdminQueue.SubmissionQueueTail = 0;
    // Start with QueueSize so phase = (QueueSize >> bits) & 1 = 1
    DevExt->AdminQueue.CompletionQueueHead = DevExt->AdminQueue.QueueSize;
    DevExt->AdminQueue.CompletionQueueTail = 0;
#ifdef NVME2K_USE_SUBMISSION_LOCK
    DevExt->AdminQueue.SubmissionLock = 0;
#endif
#ifdef NVME2K_USE_COMPLETION_LOCK
    DevExt->AdminQueue.CompletionLock = 0;
#endif

    // Initialize I/O Queue state
    DevExt->IoQueue.SubmissionQueueHead = 0;
    DevExt->IoQueue.SubmissionQueueTail = 0;
    // Start with QueueSize so phase = (QueueSize >> bits) & 1 = 1
    DevExt->IoQueue.CompletionQueueHead = DevExt->IoQueue.QueueSize;
    DevExt->IoQueue.CompletionQueueTail = 0;
#ifdef NVME2K_USE_SUBMISSION_LOCK
    DevExt->IoQueue.SubmissionLock = 0;
#endif
#ifdef NVME2K_USE_COMPLETION_LOCK
    DevExt->IoQueue.CompletionLock = 0;
#endif

    // Zero out queues
    RtlZeroMemory(DevExt->AdminQueue.SubmissionQueue, DevExt->AdminQueue.QueueSize * NVME_SQ_ENTRY_SIZE);
    RtlZeroMemory(DevExt->AdminQueue.CompletionQueue, DevExt->AdminQueue.QueueSize * NVME_CQ_ENTRY_SIZE);
    RtlZeroMemory(DevExt->IoQueue.SubmissionQueue, DevExt->IoQueue.QueueSize * NVME_SQ_ENTRY_SIZE);
    RtlZeroMemory(DevExt->IoQueue.CompletionQueue, DevExt->IoQueue.QueueSize * NVME_CQ_ENTRY_SIZE);

    // Clear the utility buffer
    RtlZeroMemory(DevExt->UtilityBuffer, PAGE_SIZE);

    // Configure Admin Queue Attributes
    aqa = ((DevExt->AdminQueue.QueueSize - 1) << 16) | (DevExt->AdminQueue.QueueSize - 1);
    NvmeWriteReg32(DevExt, NVME_REG_AQA, aqa);
    
    // Set Admin Queue addresses
    NvmeWriteReg64(DevExt, NVME_REG_ASQ, DevExt->AdminQueue.SubmissionQueuePhys.QuadPart);
    NvmeWriteReg64(DevExt, NVME_REG_ACQ, DevExt->AdminQueue.CompletionQueuePhys.QuadPart);
    
    // Configure controller
    // Calculate MPS (Memory Page Size) based on host page size.
    // The value is log2(PAGE_SIZE) - 12.
    // For 4KB (4096), pageShift = log2(4096) - 12 = 12 - 12 = 0.
    // For 8KB (8192), pageShift = log2(8192) - 12 = 13 - 12 = 1.
    pageShift = 0; // Default for 4KB pages
    if (DevExt->PageSize > 4096) {
        pageShift = (ULONG)(log2(DevExt->PageSize) - 12);
    }
    cc = NVME_CC_ENABLE | 
         (pageShift << NVME_CC_MPS_SHIFT) |
         NVME_CC_CSS_NVM |
         NVME_CC_AMS_RR |
         NVME_CC_SHN_NONE |
         NVME_CC_IOSQES |
         NVME_CC_IOCQES;
    
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc);
    
    // Wait for controller to become ready
    if (!NvmeWaitForReady(DevExt, TRUE)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwInitialize - controller failed to become ready\n");
#endif
        return FALSE;
    }

    DevExt->NamespaceSizeInBlocks = 0;
    DevExt->NamespaceBlockSize = 512;  // Default to 512 bytes

    DevExt->NextNonTaggedId = 0;  // Initialize non-tagged CID sequence
    DevExt->NonTaggedInFlight = FALSE;  // No non-tagged request in flight initially
    DevExt->NonTaggedSrbFallback = NULL;

    // Initialize statistics
    DevExt->CurrentQueueDepth = 0;
    DevExt->MaxQueueDepthReached = 0;
    DevExt->CurrentPrpListPagesUsed = 0;
    DevExt->MaxPrpListPagesUsed = 0;
    DevExt->TotalRequests = 0;
    DevExt->TotalReads = 0;
    DevExt->TotalWrites = 0;
    DevExt->TotalBytesRead = 0;
    DevExt->TotalBytesWritten = 0;
    DevExt->MaxReadSize = 0;
    DevExt->MaxWriteSize = 0;
    DevExt->RejectedRequests = 0;

    DevExt->SMARTEnabled = TRUE;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: PRP list pool initialized at VA=%p PA=%08X%08X (%d pages)\n",
                    DevExt->PrpListPages,
                    (ULONG)(DevExt->PrpListPagesPhys.QuadPart >> 32),
                    (ULONG)(DevExt->PrpListPagesPhys.QuadPart & 0xFFFFFFFF),
                    SG_LIST_PAGES);
#endif

    // Start the initialization sequence
    DevExt->InitComplete = FALSE;
    
    NvmeCreateIoCQ(DevExt);

    return TRUE;
}

//
// HwStartIo - Process SCSI request
//
BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    NVME_COMMAND nvmeCmd;
    USHORT commandId;

#ifdef NVME2K_DBG_EXTRA
    ScsiDebugPrint(0, "nvme2k: HwStartIo called - Function=%02X Path=%d Target=%d Lun=%d\n",
                   Srb->Function, Srb->PathId, Srb->TargetId, Srb->Lun);
#endif

    // Check if the request is for our device (PathId=0, TargetId=0, Lun=0)
    if (Srb->PathId != 0 || Srb->TargetId != 0 || Srb->Lun != 0) {
        // Not our device
        if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI) {
            Srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
        } else {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        }
        ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
        ScsiPortNotification(NextRequest, DeviceExtension, NULL);
        return TRUE;
    }

    // Process the SRB based on its function
    switch (Srb->Function) {
        case SRB_FUNCTION_EXECUTE_SCSI:
#ifdef NVME2K_DBG_EXTRA
            ScsiDebugPrint(0, "nvme2k: processing op=%02X\n", Srb->Cdb[0]);

            // Log tagged queuing usage
            if (Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) {
                const char* queueType = "Unknown";
                switch (Srb->QueueAction) {
                    case SRB_SIMPLE_TAG_REQUEST: queueType = "SIMPLE"; break;
                    case SRB_HEAD_OF_QUEUE_TAG_REQUEST: queueType = "HEAD_OF_QUEUE"; break;
                    case SRB_ORDERED_QUEUE_TAG_REQUEST: queueType = "ORDERED"; break;
                }
                ScsiDebugPrint(0, "nvme2k: Tagged queuing enabled - QueueAction=%s (0x%02X)\n",
                               queueType, Srb->QueueAction);
            }
#endif
            // Process SCSI CDB
            switch (Srb->Cdb[0]) {
                case SCSIOP_READ6:
                case SCSIOP_READ:
                case SCSIOP_WRITE6:
                case SCSIOP_WRITE:
                    return ScsiHandleReadWrite(DevExt, Srb);
                    
                case SCSIOP_TEST_UNIT_READY:
                    if (DevExt->InitComplete)
                        Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    else
                        Srb->SrbStatus = SRB_STATUS_BUSY;
                    break;

                case SCSIOP_VERIFY6:
                case SCSIOP_VERIFY:
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    break;
                    
                case SCSIOP_INQUIRY:
                    return ScsiHandleInquiry(DevExt, Srb);
                    
                case SCSIOP_READ_CAPACITY:
                    return ScsiHandleReadCapacity(DevExt, Srb);
                    
                case SCSIOP_LOG_SENSE:
                    return ScsiHandleLogSense(DevExt, Srb);

                case SCSIOP_MODE_SENSE:
                case SCSIOP_MODE_SENSE10:
                    return ScsiHandleModeSense(DevExt, Srb);
                    
                case SCSIOP_START_STOP_UNIT:
                    // Accept but do nothing
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    break;
                    
                case SCSIOP_SYNCHRONIZE_CACHE:
                    return ScsiHandleFlush(DevExt, Srb);

                case SCSIOP_ATA_PASSTHROUGH16:
                case SCSIOP_ATA_PASSTHROUGH12:
                    // SAT (SCSI/ATA Translation) ATA PASS-THROUGH commands
                    return ScsiHandleSatPassthrough(DevExt, Srb);

                case SCSIOP_READ_DEFECT_DATA10:
                    return ScsiHandleReadDefectData10(DevExt, Srb);

                default:
#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: HwStartIo - unimplemented SCSI opcode 0x%02X\n", Srb->Cdb[0]);
#endif
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                    break;
            }
            break;

        case SRB_FUNCTION_FLUSH:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_FLUSH\n");
#endif
            return ScsiHandleFlush(DevExt, Srb);

        case SRB_FUNCTION_FLUSH_QUEUE:
            // No internal queue to flush - requests go directly to hardware
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_SHUTDOWN:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo - SRB_FUNCTION_SHUTDOWN - flushing cache\n");
#endif
            // Flush all cached writes before shutdown
            return ScsiHandleFlush(DevExt, Srb);

        case SRB_FUNCTION_ABORT_COMMAND:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_ABORT_COMMAND\n");
#endif
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_RESET_BUS:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_RESET_BUS\n");
#endif
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_RESET_DEVICE:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo SRB_FUNCTION_RESET_DEVICE\n");
#endif
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_IO_CONTROL:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo - SRB_FUNCTION_IO_CONTROL\n");
#endif
            // Handle SMART and other miniport IOCTLs
            if (HandleIO_SCSIDISK(DevExt, Srb)) {
                // If HandleIO_SCSIDISK returns TRUE, it may have set the status
                // to PENDING for async operations.
                if (Srb->SrbStatus != SRB_STATUS_PENDING) {
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                }
            } else {
#ifdef NVME2K_DBG
                if (Srb->DataTransferLength >= sizeof(SRB_IO_CONTROL)) {
                    PSRB_IO_CONTROL srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;
                    ScsiDebugPrint(0, "nvme2k: Unhandled IO_CONTROL, Sig: %.8s\n", srbControl->Signature);
                }
#endif
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            }
            break;

        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo - unimplemented SRB function 0x%02X\n", Srb->Function);
#endif
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    // Complete the request if not pending
    if (Srb->SrbStatus != SRB_STATUS_PENDING) {
        ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
        ScsiPortNotification(NextRequest, DeviceExtension, NULL);
    }

    return TRUE;
}

//
// HwInterrupt - ISR for adapter
//
BOOLEAN HwInterrupt(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    BOOLEAN interruptHandled = FALSE;

#ifdef NVME2K_USE_INTERRUPT_LOCK
    // Serialize interrupt handler to prevent SMP reentrancy issues
    // Try to acquire lock (atomically change 0 -> 1)
    if (!AtomicCompareExchange(&DevExt->InterruptLock, 1, 0)) {
        // Another CPU is already in the interrupt handler
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwInterrupt - lock contention detected (another CPU is in ISR)\n");
#endif
        // Return FALSE to let ScsiPort try again
        return FALSE;
    }
#endif

    // Process Admin Queue completions first
    if (NvmeProcessAdminCompletion(DevExt)) {
        interruptHandled = TRUE;
    }

    // Process I/O Queue completions
    if (NvmeProcessIoCompletion(DevExt)) {
        interruptHandled = TRUE;
    }

#ifdef NVME2K_USE_INTERRUPT_LOCK
    // Release lock (atomic write to ensure memory ordering)
    AtomicSet(&DevExt->InterruptLock, 0);
#endif

    return interruptHandled;
}

//
// HwResetBus - Reset the SCSI bus
//
BOOLEAN HwResetBus(IN PVOID DeviceExtension, IN ULONG PathId)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    // TODO: Reset the SCSI bus
    // Perform hardware reset

    // Complete all outstanding requests
    ScsiPortCompleteRequest(DeviceExtension, (UCHAR)PathId, 
                           SP_UNTAGGED, SP_UNTAGGED,
                           SRB_STATUS_BUS_RESET);

    return TRUE;
}

#if (_WIN32_WINNT >= 0x500)
//
// HwAdapterControl - Handle adapter power and PnP events (Windows 2000+)
//
SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    SCSI_ADAPTER_CONTROL_STATUS status = ScsiAdapterControlSuccess;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwAdapterControl called - ControlType=%d\n", ControlType);
#endif
    switch (ControlType) {
        case ScsiQuerySupportedControlTypes:
            {
                PSCSI_SUPPORTED_CONTROL_TYPE_LIST list =
                    (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;

                // Indicate which control types we support
                list->SupportedTypeList[ScsiStopAdapter] = TRUE;
                list->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            }
            break;

        case ScsiStopAdapter:
            // Stop the adapter - perform clean shutdown
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: ScsiStopAdapter - performing shutdown\n");
#endif
            NvmeShutdownController(DevExt);
            break;

        case ScsiRestartAdapter:
            // Restart the adapter - reinitialize controller
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: ScsiRestartAdapter - reinitializing\n");
#endif
            // Controller was reset, need to reinitialize
            if (HwInitialize(DevExt)) {
                status = ScsiAdapterControlSuccess;
            } else {
                status = ScsiAdapterControlUnsuccessful;
            }
            break;

        default:
            status = ScsiAdapterControlUnsuccessful;
            break;
    }

    return status;
}
#else
//
// HwAdapterState - Handle adapter state changes (Windows NT 4)
//
VOID HwAdapterState(
    IN PVOID DeviceExtension,
    IN PVOID Context,
    IN BOOLEAN SaveState)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwAdapterState called - SaveState=%d\n", SaveState);
#endif

    if (SaveState) {
        // Save adapter state - prepare for power down or hibernation
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwAdapterState - saving state and shutting down\n");
#endif
        // Perform clean shutdown of the NVMe controller
        NvmeShutdownController(DevExt);
    } else {
        // Restore adapter state - reinitialize after power up
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwAdapterState - restoring state\n");
#endif
        // Reinitialize the controller
        HwInitialize(DevExt);
    }
}
#endif
 