//
// SCSI Miniport Driver Skeleton for Windows 2000
// This is a template implementation showing the required structure
// Includes PCI support and NVMe device detection
//

#include "nvme2k.h"

//
// DriverEntry - Main entry point
//
ULONG DriverEntry(IN PVOID DriverObject, IN PVOID Argument2)
{
    HW_INITIALIZATION_DATA hwInitData;
    ULONG status;

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
    hwInitData.HwAdapterControl = HwAdapterControl;

    // Set driver-specific parameters
    hwInitData.AdapterInterfaceType = PCIBus;  // Change as needed
    hwInitData.DeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    hwInitData.SpecificLuExtensionSize = 0;
    hwInitData.SrbExtensionSize = 0;
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

    // Call port driver
    status = ScsiPortInitialize(DriverObject, Argument2,
                                &hwInitData, NULL);

    ScsiDebugPrint(0, "nvme2k: DriverEntry exiting with status 0x%08X\n", status);

    return status;
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
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    PACCESS_RANGE accessRange;
    PCI_SLOT_NUMBER slotData;
    UCHAR pciBuffer[256];
    ULONG slotNumber;
    ULONG busNumber;
    UCHAR baseClass, subClass, progIf;
    BOOLEAN deviceFound = FALSE;
    ULONG bytesRead;
    ULONG bar0;
    ULONG barSize;
    UCHAR tempBuffer[256];
    ULONG tempSize;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter called\n");
#endif
    // Initialize for PCI scanning
    slotData.u.AsULONG = 0;
    
    // Start scanning from where we left off
    if (HwContext != NULL) {
        busNumber = ((PULONG)HwContext)[0];
        slotNumber = ((PULONG)HwContext)[1];
    } else {
        busNumber = ConfigInfo->SystemIoBusNumber;
        slotNumber = 0;
    }

    // Scan PCI bus for NVMe devices
    for (; busNumber < 256 && !deviceFound; busNumber++) {
        for (; slotNumber < PCI_MAX_DEVICES * PCI_MAX_FUNCTION; slotNumber++) {
            slotData.u.AsULONG = slotNumber;

            // Read PCI configuration space
            bytesRead = ScsiPortGetBusData(
                DeviceExtension,
                PCIConfiguration,
                busNumber,
                slotNumber,
                pciBuffer,
                256);

            if (bytesRead == 0) {
                continue;  // No device in this slot
            }

            // Extract Vendor ID and Device ID
            devExt->VendorId = *(USHORT*)&pciBuffer[PCI_VENDOR_ID_OFFSET];
            devExt->DeviceId = *(USHORT*)&pciBuffer[PCI_DEVICE_ID_OFFSET];

            // Check for invalid vendor ID
            if (devExt->VendorId == 0xFFFF || devExt->VendorId == 0x0000) {
                continue;
            }

            // Extract class code information
            devExt->RevisionId = pciBuffer[PCI_REVISION_ID_OFFSET];
            progIf = pciBuffer[PCI_CLASS_CODE_OFFSET];
            subClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 1];
            baseClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 2];

#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwFindAdapter - checking bus %d slot %d: VID=%04X DID=%04X Class=%02X%02X%02X\n",
                           busNumber, slotNumber, devExt->VendorId, devExt->DeviceId,
                           baseClass, subClass, progIf);
#endif
            // Check if this is an NVMe device
            if (IsNvmeDevice(baseClass, subClass, progIf)) {
                deviceFound = TRUE;
                devExt->BusNumber = busNumber;
                devExt->SlotNumber = slotNumber;
                break;
            }
        }
        
        if (deviceFound) {
            break;
        }
        slotNumber = 0;  // Reset slot for next bus
    }

    if (!deviceFound) {
        *Again = FALSE;
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFindAdapter - no NVMe device found\n");
#endif
        return SP_RETURN_NOT_FOUND;
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - found device VID=%04X DID=%04X at bus %d slot %d\n",
                   devExt->VendorId, devExt->DeviceId, devExt->BusNumber, devExt->SlotNumber);
#endif
    // Read subsystem IDs using helper function
    devExt->SubsystemVendorId = ReadPciConfigWord(DeviceExtension, PCI_SUBSYSTEM_VENDOR_ID_OFFSET);
    devExt->SubsystemId = ReadPciConfigWord(DeviceExtension, PCI_SUBSYSTEM_ID_OFFSET);

    // Enable PCI device (Bus Master, Memory Space)
    WritePciConfigWord(DeviceExtension, PCI_COMMAND_OFFSET, 
                      PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE);

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
    ConfigInfo->Dma64BitAddresses = TRUE;  // NVMe supports 64-bit addressing
    ConfigInfo->MaximumNumberOfTargets = 2;  // Support TargetId 0 and 1
    ConfigInfo->NumberOfPhysicalBreaks = 511;  // PRP1 + PRP list (512 entries)
    ConfigInfo->AlignmentMask = 0x3;  // DWORD alignment
    ConfigInfo->NeedPhysicalAddresses = TRUE;  // Required for ScsiPortGetPhysicalAddress to work
    ConfigInfo->TaggedQueuing = TRUE;  // Support tagged command queuing
    ConfigInfo->MultipleRequestPerLu = TRUE;  // Allow multiple outstanding commands per LUN
    ConfigInfo->AutoRequestSense = TRUE;  // Automatically provide sense data on errors
    ConfigInfo->SrbExtensionSize = sizeof(NVME_SRB_EXTENSION);  // Per-SRB data for PRP list tracking

    // Get BAR0 (Controller registers for NVMe)
    accessRange = &((*(ConfigInfo->AccessRanges))[0]);
    accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(0);
    accessRange->RangeLength = 0;

    bar0 = ReadPciConfigDword(DeviceExtension, PCI_BASE_ADDRESS_0);

    if (bar0 & 0x1) {
        // I/O Space (unlikely for NVMe but handle it)
        accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(bar0 & 0xFFFFFFFC);
        accessRange->RangeInMemory = FALSE;
    } else {
        // Memory Space (typical for NVMe)
        accessRange->RangeStart = ScsiPortConvertUlongToPhysicalAddress(bar0 & 0xFFFFFFF0);
        accessRange->RangeInMemory = TRUE;
    }

    WritePciConfigDword(DeviceExtension, PCI_BASE_ADDRESS_0, 0xFFFFFFFF);

    // Read back the modified value
    barSize = ReadPciConfigDword(DeviceExtension, PCI_BASE_ADDRESS_0);

    // Restore original BAR value
    WritePciConfigDword(DeviceExtension, PCI_BASE_ADDRESS_0, bar0);

    // Calculate size
    // The size is the bitwise NOT of the value read back (ignoring type bits), plus one.
    if (accessRange->RangeInMemory) {
        accessRange->RangeLength = ~(barSize & 0xFFFFFFF0) + 1;
    } else {
        accessRange->RangeLength = ~(barSize & 0xFFFFFFFC) + 1;
    }

    devExt->ControllerRegistersLength = accessRange->RangeLength;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - BAR0 base=0x%08X size=0x%08X %s\n",
                   (ULONG)accessRange->RangeStart.QuadPart,
                   accessRange->RangeLength,
                   accessRange->RangeInMemory ? "Memory" : "I/O");
#endif
    // Map the controller registers
    devExt->ControllerRegisters = ScsiPortGetDeviceBase(
        DeviceExtension,
        ConfigInfo->AdapterInterfaceType,
        ConfigInfo->SystemIoBusNumber,
        accessRange->RangeStart,
        accessRange->RangeLength,
        (BOOLEAN)!accessRange->RangeInMemory);

    if (devExt->ControllerRegisters == NULL) {
        ScsiDebugPrint(0, "nvme2k: HwFindAdapter - failed to map controller registers\n");
        return SP_RETURN_ERROR;
    }
#if 0    
// can be reported in newer OSes
    // Read controller capabilities to determine max queue size
    // This must be done here so we can set ConfigInfo->NumberOfRequests
    {
        ULONGLONG cap;
        USHORT mqes, maxQueueSize;

        cap = NvmeReadReg64(devExt, NVME_REG_CAP);
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
    devExt->UncachedExtensionSize = UNCACHED_EXTENSION_SIZE;

    devExt->UncachedExtensionBase = ScsiPortGetUncachedExtension(
        DeviceExtension,
        ConfigInfo,
        devExt->UncachedExtensionSize);

    if (devExt->UncachedExtensionBase == NULL) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwFindAdapter - failed to allocate uncached memory\n");
#endif
        return SP_RETURN_ERROR;
    }

    // Get physical address of the entire uncached block
    tempSize = devExt->UncachedExtensionSize;
    devExt->UncachedExtensionPhys = ScsiPortGetPhysicalAddress(
        DeviceExtension,
        NULL,
        devExt->UncachedExtensionBase,
        &tempSize);
    
    // Zero out the entire uncached memory
    RtlZeroMemory(devExt->UncachedExtensionBase, devExt->UncachedExtensionSize);

    // Initialize allocator
    devExt->UncachedExtensionOffset = 0;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - allocated %u bytes of uncached memory at virt=%p phys=%08X%08X\n",
                   devExt->UncachedExtensionSize, devExt->UncachedExtensionBase,
                   (ULONG)(devExt->UncachedExtensionPhys.QuadPart >> 32),
                   (ULONG)(devExt->UncachedExtensionPhys.QuadPart & 0xFFFFFFFF));
#endif
    // Check if there are more devices to scan
    if (busNumber < 255 || slotNumber < (PCI_MAX_DEVICES * PCI_MAX_FUNCTION - 1)) {
        *Again = TRUE;
        // Save context for next call
        if (HwContext == NULL) {
            HwContext = &ConfigInfo->SystemIoBusNumber;
        }
        ((PULONG)HwContext)[0] = busNumber;
        ((PULONG)HwContext)[1] = slotNumber + 1;
    } else {
        *Again = FALSE;
    }

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFindAdapter - success, returning SP_RETURN_FOUND\n");
#endif
    return SP_RETURN_FOUND;
}

//
// HwInitialize - Initialize the adapter
//
BOOLEAN HwInitialize(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    ULONG cc, aqa;
    ULONG pageShift;

    // Read controller capabilities
    devExt->ControllerCapabilities = NvmeReadReg64(devExt, NVME_REG_CAP);
    devExt->Version = NvmeReadReg32(devExt, NVME_REG_VS);

    // Parse MQES (Maximum Queue Entries Supported) from CAP register
    // MQES is 0-based, so actual max queue size is MQES + 1
    devExt->MaxQueueEntries = (USHORT)((devExt->ControllerCapabilities & NVME_CAP_MQES_MASK) + 1);

    // Calculate doorbell stride (in bytes)
    devExt->DoorbellStride = 4 << (((ULONG)(devExt->ControllerCapabilities >> 32) & 0xF));

    // Determine page size (4KB minimum)
    devExt->PageSize = PAGE_SIZE;
    
    // Disable controller first
    cc = NvmeReadReg32(devExt, NVME_REG_CC);
    cc &= ~NVME_CC_ENABLE;
    NvmeWriteReg32(devExt, NVME_REG_CC, cc);
    
    // Wait for controller to become not ready
    if (!NvmeWaitForReady(devExt, FALSE)) {
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
        if (queueSize > devExt->MaxQueueEntries) {
            queueSize = devExt->MaxQueueEntries;
        }

        // 1. Allocate Admin SQ (4KB aligned)
        devExt->AdminQueue.QueueSize = queueSize;
        devExt->AdminQueue.QueueId = 0;
        if (!AllocateUncachedMemory(devExt, queueSize * NVME_SQ_ENTRY_SIZE, PAGE_SIZE,
                                    &devExt->AdminQueue.SubmissionQueue,
                                    &devExt->AdminQueue.SubmissionQueuePhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate admin SQ\n");
#endif
            return FALSE;
        }

        // 2. Allocate I/O SQ (4KB aligned)
        devExt->IoQueue.QueueSize = queueSize;
        devExt->IoQueue.QueueId = 1;
        if (!AllocateUncachedMemory(devExt, queueSize * NVME_SQ_ENTRY_SIZE, PAGE_SIZE,
                                    &devExt->IoQueue.SubmissionQueue,
                                    &devExt->IoQueue.SubmissionQueuePhys)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate I/O SQ\n");
#endif
        return FALSE;
    }

        // 3. Allocate utility buffer (large enough for SG_LIST_PAGES * 4KB)
        // During init: used for Identify commands
        // After init: repurposed as PRP list page pool
        if (!AllocateUncachedMemory(devExt, SG_LIST_PAGES * PAGE_SIZE, PAGE_SIZE,
                                    &devExt->UtilityBuffer,
                                    &devExt->UtilityBufferPhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate utility buffer\n");
#endif
            return FALSE;
        }

        // 4. Allocate Admin CQ (must be page-aligned for NVMe)
        if (!AllocateUncachedMemory(devExt, queueSize * NVME_CQ_ENTRY_SIZE, PAGE_SIZE,
                                    &devExt->AdminQueue.CompletionQueue,
                                    &devExt->AdminQueue.CompletionQueuePhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate admin CQ\n");
#endif
            return FALSE;
        }

        // 5. Allocate I/O CQ (must be page-aligned for NVMe)
        if (!AllocateUncachedMemory(devExt, queueSize * NVME_CQ_ENTRY_SIZE, PAGE_SIZE,
                                    &devExt->IoQueue.CompletionQueue,
                                    &devExt->IoQueue.CompletionQueuePhys)) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwInitialize - failed to allocate I/O CQ\n");
#endif
            return FALSE;
        }
    }

    // Now all uncached memory is allocated - log final usage
#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwInitialize - uncached memory usage: %u / %u bytes\n",
                   devExt->UncachedExtensionOffset, devExt->UncachedExtensionSize);
#endif

    // Calculate log2(QueueSize) and mask for both queues (must be power of 2)
    {
        USHORT size, bits;

        // Admin queue
        size = devExt->AdminQueue.QueueSize;
        for (bits = 0; size > 1; bits++) {
            size >>= 1;
        }
        devExt->AdminQueue.QueueSizeBits = (UCHAR)bits;
        devExt->AdminQueue.QueueSizeMask = devExt->AdminQueue.QueueSize - 1;

        // I/O queue
        size = devExt->IoQueue.QueueSize;
        for (bits = 0; size > 1; bits++) {
            size >>= 1;
        }
        devExt->IoQueue.QueueSizeBits = (UCHAR)bits;
        devExt->IoQueue.QueueSizeMask = devExt->IoQueue.QueueSize - 1;
    }

    // Initialize SMP synchronization
#ifdef NVME2K_USE_INTERRUPT_LOCK
    devExt->InterruptLock = 0;
#endif

    // Initialize Admin Queue state
    devExt->AdminQueue.SubmissionQueueHead = 0;
    devExt->AdminQueue.SubmissionQueueTail = 0;
    // Start with QueueSize so phase = (QueueSize >> bits) & 1 = 1
    devExt->AdminQueue.CompletionQueueHead = devExt->AdminQueue.QueueSize;
    devExt->AdminQueue.CompletionQueueTail = 0;
#ifdef NVME2K_USE_SUBMISSION_LOCK
    devExt->AdminQueue.SubmissionLock = 0;
#endif
#ifdef NVME2K_USE_COMPLETION_LOCK
    devExt->AdminQueue.CompletionLock = 0;
#endif

    // Initialize I/O Queue state
    devExt->IoQueue.SubmissionQueueHead = 0;
    devExt->IoQueue.SubmissionQueueTail = 0;
    // Start with QueueSize so phase = (QueueSize >> bits) & 1 = 1
    devExt->IoQueue.CompletionQueueHead = devExt->IoQueue.QueueSize;
    devExt->IoQueue.CompletionQueueTail = 0;
#ifdef NVME2K_USE_SUBMISSION_LOCK
    devExt->IoQueue.SubmissionLock = 0;
#endif
#ifdef NVME2K_USE_COMPLETION_LOCK
    devExt->IoQueue.CompletionLock = 0;
#endif

    // Zero out queues
    RtlZeroMemory(devExt->AdminQueue.SubmissionQueue, devExt->AdminQueue.QueueSize * NVME_SQ_ENTRY_SIZE);
    RtlZeroMemory(devExt->AdminQueue.CompletionQueue, devExt->AdminQueue.QueueSize * NVME_CQ_ENTRY_SIZE);
    RtlZeroMemory(devExt->IoQueue.SubmissionQueue, devExt->IoQueue.QueueSize * NVME_SQ_ENTRY_SIZE);
    RtlZeroMemory(devExt->IoQueue.CompletionQueue, devExt->IoQueue.QueueSize * NVME_CQ_ENTRY_SIZE);

    // Clear the utility buffer
    RtlZeroMemory(devExt->UtilityBuffer, PAGE_SIZE);

    // Configure Admin Queue Attributes
    aqa = ((devExt->AdminQueue.QueueSize - 1) << 16) | (devExt->AdminQueue.QueueSize - 1);
    NvmeWriteReg32(devExt, NVME_REG_AQA, aqa);
    
    // Set Admin Queue addresses
    NvmeWriteReg64(devExt, NVME_REG_ASQ, devExt->AdminQueue.SubmissionQueuePhys.QuadPart);
    NvmeWriteReg64(devExt, NVME_REG_ACQ, devExt->AdminQueue.CompletionQueuePhys.QuadPart);
    
    // Configure controller
    pageShift = 0;  // 4KB pages (2^12)
    cc = NVME_CC_ENABLE | 
         (pageShift << NVME_CC_MPS_SHIFT) |
         NVME_CC_CSS_NVM |
         NVME_CC_AMS_RR |
         NVME_CC_SHN_NONE |
         NVME_CC_IOSQES |
         NVME_CC_IOCQES;
    
    NvmeWriteReg32(devExt, NVME_REG_CC, cc);
    
    // Wait for controller to become ready
    if (!NvmeWaitForReady(devExt, TRUE)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwInitialize - controller failed to become ready\n");
#endif
        return FALSE;
    }

    devExt->NamespaceSizeInBlocks = 0;
    devExt->NamespaceBlockSize = 512;  // Default to 512 bytes

    devExt->NextNonTaggedId = 0;  // Initialize non-tagged CID sequence
    devExt->NonTaggedInFlight = FALSE;  // No non-tagged request in flight initially

    // Start the initialization sequence
    devExt->InitComplete = FALSE;
    
    NvmeCreateIoCQ(devExt);

    return TRUE;
}

//
// HandleSmartIoctl - Process SMART/ATA pass-through IOCTLs
//
BOOLEAN HandleSmartIoctl(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PSRB_IO_CONTROL srbControl;
    PSENDCMDINPARAMS sendCmdIn;
    PSENDCMDOUTPARAMS sendCmdOut;
    PIDEREGS regs;
    ULONG dataBufferSize;
    ULONG requiredSize;
    NVME_SMART_INFO smartInfo;
    PHYSICAL_ADDRESS smartPhys;
    ULONG returnedLength;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HandleSmartIoctl called - DataTransferLength=%u\n",
                   Srb->DataTransferLength);
#endif

    // Validate minimum size for SRB_IO_CONTROL header
    if (Srb->DataTransferLength < sizeof(SRB_IO_CONTROL)) {
        return FALSE;
    }

    srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;

    // Check signature (Windows 2000 uses "SCSIDISK" for ATA pass-through)
    if (RtlCompareMemory(srbControl->Signature, "SCSIDISK", 8) != 8) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: Invalid SRB_IO_CONTROL signature\n");
#endif
        return FALSE;
    }

    // Validate that we have enough space for SENDCMDINPARAMS
    requiredSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDINPARAMS) - 1;
    if (Srb->DataTransferLength < requiredSize) {
        return FALSE;
    }

    // Get the SENDCMDINPARAMS structure (follows SRB_IO_CONTROL header)
    sendCmdIn = (PSENDCMDINPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));
    sendCmdOut = (PSENDCMDOUTPARAMS)((PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL));
    regs = &sendCmdIn->irDriveRegs;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: SMART IOCTL - Cmd=0x%02X Feature=0x%02X CylLow=0x%02X CylHi=0x%02X\n",
                   regs->bCommandReg, regs->bFeaturesReg, regs->bCylLowReg, regs->bCylHighReg);
#endif

    // Validate SMART signature in cylinder registers
    if (regs->bCylLowReg != SMART_CYL_LOW || regs->bCylHighReg != SMART_CYL_HI) {
        // Not a SMART command, could be IDENTIFY or other ATA command
        if (regs->bCommandReg == ATA_IDENTIFY_DEVICE) {
            // Handle IDENTIFY DEVICE by returning controller identify data
            // For now, return not supported
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: ATA IDENTIFY_DEVICE not yet implemented\n");
#endif
            return FALSE;
        }
        return FALSE;
    }

    // Handle SMART command (0xB0)
    if (regs->bCommandReg != ATA_SMART_CMD) {
        return FALSE;
    }

    // Process based on SMART subcommand (features register)
    switch (regs->bFeaturesReg) {
        case ATA_SMART_READ_DATA:
            // Read SMART data / attributes - translate to NVMe Get Log Page
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SMART READ DATA/ATTRIBS request\n");
#endif
            // Ensure we have buffer space for output
            dataBufferSize = sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + 512 - 1;
            if (Srb->DataTransferLength < dataBufferSize) {
                return FALSE;
            }

            // Issue async NVMe Get Log Page command for SMART/Health info
            // The command will complete in the interrupt handler which will copy data
            if (!NvmeGetLogPage(devExt, Srb, NVME_LOG_PAGE_SMART_HEALTH)) {
#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: Failed to submit Get Log Page command\n");
#endif
                return FALSE;
            }

            // Mark SRB as pending - will be completed in interrupt handler
            Srb->SrbStatus = SRB_STATUS_PENDING;
            return TRUE;

        case ATA_SMART_RETURN_STATUS:
            // Return SMART status - check if device is healthy
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SMART RETURN STATUS request\n");
#endif
            // Initialize output structure
            RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
            sendCmdOut->cBufferSize = 0;
            sendCmdOut->DriverStatus.bDriverError = 0;  // Success
            sendCmdOut->DriverStatus.bIDEError = 0;     // No error

            // In ATA SMART, threshold exceeded is indicated by:
            // Cylinder registers: 0x2C (low) and 0xF4 (high) = FAILING
            // Cylinder registers: 0x4F (low) and 0xC2 (high) = PASSING
            // We'd need to read NVMe SMART critical warning to determine this
            // For now, return passing status
            regs->bCylLowReg = SMART_CYL_LOW;   // 0x4F = passing
            regs->bCylHighReg = SMART_CYL_HI;   // 0xC2 = passing

            srbControl->ReturnCode = 0;  // Success
            return TRUE;

        case ATA_SMART_ENABLE:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SMART ENABLE (NVMe always enabled)\n");
#endif
            // NVMe SMART is always enabled
            RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
            sendCmdOut->DriverStatus.bDriverError = 0;
            srbControl->ReturnCode = 0;
            return TRUE;

        case ATA_SMART_DISABLE:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: SMART DISABLE (NVMe cannot disable)\n");
#endif
            // NVMe SMART cannot be disabled, return success anyway
            RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
            sendCmdOut->DriverStatus.bDriverError = 0;
            srbControl->ReturnCode = 0;
            return TRUE;

        case ATA_SMART_AUTOSAVE:
        case ATA_SMART_READ_THRESHOLDS:
        default:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Unsupported SMART subcommand 0x%02X\n",
                           regs->bFeaturesReg);
#endif
            return FALSE;
    }
}

//
// HwStartIo - Process SCSI request
//
BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
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
                    ScsiHandleReadWrite(devExt, Srb);
                    return TRUE;
                    
                case SCSIOP_TEST_UNIT_READY:
                    if (devExt->InitComplete)
                        Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    else
                        Srb->SrbStatus = SRB_STATUS_BUSY;
                    break;

                case SCSIOP_VERIFY6:
                case SCSIOP_VERIFY:
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    break;
                    
                case SCSIOP_INQUIRY:
                    ScsiHandleInquiry(devExt, Srb);
                    break;
                    
                case SCSIOP_READ_CAPACITY:
                    ScsiHandleReadCapacity(devExt, Srb);
                    break;
                    
                case SCSIOP_MODE_SENSE:
                case SCSIOP_MODE_SENSE10:
                    // Return minimal mode sense data
                    if (Srb->DataTransferLength >= 4) {
                        RtlZeroMemory(Srb->DataBuffer, Srb->DataTransferLength);
                        ((PUCHAR)Srb->DataBuffer)[0] = 0x03;  // Mode data length
                    }
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    break;
                    
                case SCSIOP_START_STOP_UNIT:
                    // Accept but do nothing
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                    break;
                    
                case SCSIOP_SYNCHRONIZE_CACHE:
                    ScsiHandleFlush(devExt, Srb);
                    return TRUE;

                default:
#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: HwStartIo - unimplemented SCSI opcode 0x%02X\n", Srb->Cdb[0]);
#endif
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                    break;
            }
            break;

        case SRB_FUNCTION_FLUSH:
            ScsiHandleFlush(devExt, Srb);
            return TRUE;

        case SRB_FUNCTION_FLUSH_QUEUE:
            // No internal queue to flush - requests go directly to hardware
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_SHUTDOWN:
            ScsiDebugPrint(0, "nvme2k: HwStartIo - SRB_FUNCTION_SHUTDOWN - flushing cache\n");
            // Flush all cached writes before shutdown
            ScsiHandleFlush(devExt, Srb);
            return TRUE;

        case SRB_FUNCTION_ABORT_COMMAND:
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_RESET_BUS:
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_RESET_DEVICE:
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;

        case SRB_FUNCTION_IO_CONTROL:
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: HwStartIo - SRB_FUNCTION_IO_CONTROL\n");
#endif
            // Handle SMART and other miniport IOCTLs
            if (HandleSmartIoctl(devExt, Srb)) {
                // If HandleSmartIoctl returns TRUE, it may have set the status
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
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    BOOLEAN interruptHandled = FALSE;

#ifdef NVME2K_USE_INTERRUPT_LOCK
    // Serialize interrupt handler to prevent SMP reentrancy issues
    // Try to acquire lock (atomically change 0 -> 1)
    if (!AtomicCompareExchange(&devExt->InterruptLock, 1, 0)) {
        // Another CPU is already in the interrupt handler
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: HwInterrupt - lock contention detected (another CPU is in ISR)\n");
#endif
        // Return FALSE to let ScsiPort try again
        return FALSE;
    }
#endif

    // Process Admin Queue completions first
    if (NvmeProcessAdminCompletion(devExt)) {
        interruptHandled = TRUE;
    }

    // Process I/O Queue completions
    if (NvmeProcessIoCompletion(devExt)) {
        interruptHandled = TRUE;
    }

#ifdef NVME2K_USE_INTERRUPT_LOCK
    // Release lock (atomic write to ensure memory ordering)
    AtomicSet(&devExt->InterruptLock, 0);
#endif

    return interruptHandled;
}

//
// HwResetBus - Reset the SCSI bus
//
BOOLEAN HwResetBus(IN PVOID DeviceExtension, IN ULONG PathId)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    // TODO: Reset the SCSI bus
    // Perform hardware reset

    // Complete all outstanding requests
    ScsiPortCompleteRequest(DeviceExtension, (UCHAR)PathId, 
                           SP_UNTAGGED, SP_UNTAGGED,
                           SRB_STATUS_BUS_RESET);

    return TRUE;
}

//
// AllocatePrpListPage - Allocate a PRP list page from the pool
// Returns page index (0-9) or 0xFF if none available
//
UCHAR AllocatePrpListPage(IN PHW_DEVICE_EXTENSION devExt)
{
    UCHAR i;

    for (i = 0; i < SG_LIST_PAGES; i++) {
        if ((devExt->PrpListPageBitmap & (1 << i)) == 0) {
            // Found free page
            devExt->PrpListPageBitmap |= (1 << i);
#ifdef NVME2K_DBG_TOOMUCH
            ScsiDebugPrint(0, "nvme2k: Allocated PRP list page %d\n", i);
#endif

            // Track maximum PRP list pages used
            devExt->CurrentPrpListPagesUsed++;
            if (devExt->CurrentPrpListPagesUsed > devExt->MaxPrpListPagesUsed) {
                devExt->MaxPrpListPagesUsed = devExt->CurrentPrpListPagesUsed;
            }

            return i;
        }
    }

#ifdef NVME2K_DBG_TOOMUCH
    ScsiDebugPrint(0, "nvme2k: No free PRP list pages available\n");
#endif
    return 0xFF;  // No pages available
}

//
// FreePrpListPage - Free a PRP list page back to the pool
//
VOID FreePrpListPage(IN PHW_DEVICE_EXTENSION devExt, IN UCHAR pageIndex)
{
    if (pageIndex < SG_LIST_PAGES) {
        devExt->PrpListPageBitmap &= ~(1 << pageIndex);
        devExt->CurrentPrpListPagesUsed--;
#ifdef NVME2K_DBG_TOOMUCH
        ScsiDebugPrint(0, "nvme2k: Freed PRP list page %d\n", pageIndex);
#endif
    }
}

//
// GetPrpListPageVirtual - Get virtual address of a PRP list page
//
PVOID GetPrpListPageVirtual(IN PHW_DEVICE_EXTENSION devExt, IN UCHAR pageIndex)
{
    if (pageIndex >= SG_LIST_PAGES) {
        return NULL;
    }
    return (PVOID)((PUCHAR)devExt->PrpListPages + (pageIndex * PAGE_SIZE));
}

//
// GetPrpListPagePhysical - Get physical address of a PRP list page
//
PHYSICAL_ADDRESS GetPrpListPagePhysical(IN PHW_DEVICE_EXTENSION devExt, IN UCHAR pageIndex)
{
    PHYSICAL_ADDRESS addr;

    if (pageIndex >= SG_LIST_PAGES) {
        addr.QuadPart = 0;
        return addr;
    }

    addr.QuadPart = devExt->PrpListPagesPhys.QuadPart + (pageIndex * PAGE_SIZE);
    return addr;
}

//
// AllocateUncachedMemory - Allocate memory from uncached extension with alignment
//
BOOLEAN AllocateUncachedMemory(
    IN PHW_DEVICE_EXTENSION devExt,
    IN ULONG Size,
    IN ULONG Alignment,
    OUT PVOID* VirtualAddress,
    OUT PHYSICAL_ADDRESS* PhysicalAddress)
{
    ULONG alignedOffset;
    ULONG alignmentMask;

    // Calculate alignment mask (e.g., 0x1000 - 1 = 0xFFF)
    alignmentMask = Alignment - 1;

    // Align current offset up to the requested alignment
    alignedOffset = (devExt->UncachedExtensionOffset + alignmentMask) & ~alignmentMask;

    // Check if we have enough space
    if (alignedOffset + Size > devExt->UncachedExtensionSize) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: AllocateUncachedMemory - OUT OF MEMORY! Need %u bytes at offset %u, total %u\n",
                       Size, alignedOffset, devExt->UncachedExtensionSize);
        ScsiDebugPrint(0, "nvme2k: AllocateUncachedMemory - Increase UNCACHED_EXTENSION_SIZE in nvme2k.h!\n");
#endif
        return FALSE;
    }

    // Calculate addresses
    *VirtualAddress = (PUCHAR)devExt->UncachedExtensionBase + alignedOffset;
    PhysicalAddress->QuadPart = devExt->UncachedExtensionPhys.QuadPart + alignedOffset;

    // Update offset for next allocation
    devExt->UncachedExtensionOffset = alignedOffset + Size;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: AllocateUncachedMemory - allocated %u bytes at offset %u (virt=%p phys=%08X%08X)\n",
                   Size, alignedOffset, *VirtualAddress,
                   (ULONG)(PhysicalAddress->QuadPart >> 32),
                   (ULONG)(PhysicalAddress->QuadPart & 0xFFFFFFFF));
#endif
    return TRUE;
}

//
// HwAdapterControl - Handle adapter power and PnP events
//
SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
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
            // TODO: Stop the adapter
            break;

        case ScsiRestartAdapter:
            // TODO: Restart the adapter
            break;

        default:
            status = ScsiAdapterControlUnsuccessful;
            break;
    }

    return status;
}

//
// Helper Functions
//

//
// IsNvmeDevice - Check if PCI class codes match NVMe
//
BOOLEAN IsNvmeDevice(UCHAR BaseClass, UCHAR SubClass, UCHAR ProgIf)
{
    return (BaseClass == PCI_CLASS_MASS_STORAGE_CONTROLLER &&
            SubClass == PCI_SUBCLASS_NON_VOLATILE_MEMORY &&
            ProgIf == PCI_PROGIF_NVME);
}

//
// ReadPciConfigWord - Read 16-bit value from PCI config space
//
USHORT ReadPciConfigWord(IN PVOID DeviceExtension, IN ULONG Offset)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    UCHAR buffer[256];

    ScsiPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        devExt->BusNumber,
        devExt->SlotNumber,
        buffer,
        256);

    return *(USHORT*)&buffer[Offset];
}

//
// ReadPciConfigDword - Read 32-bit value from PCI config space
//
ULONG ReadPciConfigDword(IN PVOID DeviceExtension, IN ULONG Offset)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    UCHAR buffer[256];

    ScsiPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        devExt->BusNumber,
        devExt->SlotNumber,
        buffer,
        256);

    return *(ULONG*)&buffer[Offset];
}

//
// WritePciConfigWord - Write 16-bit value to PCI config space
//
VOID WritePciConfigWord(IN PVOID DeviceExtension, IN ULONG Offset, IN USHORT Value)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    ScsiPortSetBusDataByOffset(
        DeviceExtension,
        PCIConfiguration,
        devExt->BusNumber,
        devExt->SlotNumber,
        &Value,
        Offset,
        sizeof(USHORT));
}

//
// WritePciConfigDword - Write 32-bit value to PCI config space
//
VOID WritePciConfigDword(IN PVOID DeviceExtension, IN ULONG Offset, IN ULONG Value)
{
    PHW_DEVICE_EXTENSION devExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    ScsiPortSetBusDataByOffset(
        DeviceExtension,
        PCIConfiguration,
        devExt->BusNumber,
        devExt->SlotNumber,
        &Value,
        Offset,
        sizeof(ULONG));
}

//
// NVMe Register Access Functions
//

ULONG NvmeReadReg32(IN PHW_DEVICE_EXTENSION devExt, IN ULONG Offset)
{
    return ScsiPortReadRegisterUlong((PULONG)((PUCHAR)devExt->ControllerRegisters + Offset));
}

VOID NvmeWriteReg32(IN PHW_DEVICE_EXTENSION devExt, IN ULONG Offset, IN ULONG Value)
{
    ScsiPortWriteRegisterUlong((PULONG)((PUCHAR)devExt->ControllerRegisters + Offset), Value);
}

ULONGLONG NvmeReadReg64(IN PHW_DEVICE_EXTENSION devExt, IN ULONG Offset)
{
    ULONGLONG value;
    PULONG ptr = (PULONG)((PUCHAR)devExt->ControllerRegisters + Offset);
    
    value = ScsiPortReadRegisterUlong(ptr);
    value |= ((ULONGLONG)ScsiPortReadRegisterUlong(ptr + 1)) << 32;
    
    return value;
}

VOID NvmeWriteReg64(IN PHW_DEVICE_EXTENSION devExt, IN ULONG Offset, IN ULONGLONG Value)
{
    PULONG ptr = (PULONG)((PUCHAR)devExt->ControllerRegisters + Offset);
    
    ScsiPortWriteRegisterUlong(ptr, (ULONG)(Value & 0xFFFFFFFF));
    ScsiPortWriteRegisterUlong(ptr + 1, (ULONG)(Value >> 32));
}

//
// NvmeWaitForReady - Wait for controller ready status
//
BOOLEAN NvmeWaitForReady(IN PHW_DEVICE_EXTENSION devExt, IN BOOLEAN WaitForReady)
{
    ULONG timeout = 5000;  // 5 seconds
    ULONG csts;
    
    while (timeout > 0) {
        csts = NvmeReadReg32(devExt, NVME_REG_CSTS);
        
        if (WaitForReady) {
            if (csts & NVME_CSTS_RDY) {
                return TRUE;
            }
        } else {
            if (!(csts & NVME_CSTS_RDY)) {
                return TRUE;
            }
        }
        
        ScsiPortStallExecution(1000);  // Wait 1ms
        timeout--;
    }
    
    return FALSE;
}

//
// NvmeRingDoorbell - Ring submission or completion queue doorbell
//
VOID NvmeRingDoorbell(IN PHW_DEVICE_EXTENSION devExt, IN USHORT QueueId, IN BOOLEAN IsSubmission, IN USHORT Value)
{
    ULONG offset;

    offset = NVME_REG_DBS + (2 * QueueId * devExt->DoorbellStride);
    if (!IsSubmission) {
        offset += devExt->DoorbellStride;
    }

    NvmeWriteReg32(devExt, offset, Value);
}

//
// NvmeSubmitCommand - Submit a command to a queue
//
BOOLEAN NvmeSubmitCommand(IN PHW_DEVICE_EXTENSION devExt, IN PNVME_QUEUE Queue, IN PNVME_COMMAND Cmd)
{
    PNVME_COMMAND sqEntry;
    USHORT nextTail;
    ULONG currentHead;
    BOOLEAN result;

#ifdef NVME2K_USE_SUBMISSION_LOCK
    // Acquire submission queue spinlock - protects against concurrent HwStartIo calls
    if (!AtomicCompareExchange(&Queue->SubmissionLock, 1, 0)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeSubmitCommand - lock contention on QID=%d (spinning)\n", Queue->QueueId);
#endif
        while (!AtomicCompareExchange(&Queue->SubmissionLock, 1, 0)) {
            // Spin waiting for lock
            // On a busy system this is very brief since we only hold it for microseconds
        }
    }
#endif

    // Check if queue is full - SubmissionQueueHead is protected by lock
    nextTail = (USHORT)((Queue->SubmissionQueueTail + 1) & Queue->QueueSizeMask);
    currentHead = (USHORT)(Queue->SubmissionQueueHead & Queue->QueueSizeMask);
    if (nextTail == currentHead) {
        // Queue full - release lock and return
#ifdef NVME2K_USE_SUBMISSION_LOCK
        AtomicSet(&Queue->SubmissionLock, 0);
#endif
        return FALSE;
    }

    // Get submission queue entry
    sqEntry = (PNVME_COMMAND)((PUCHAR)Queue->SubmissionQueue +
                              (Queue->SubmissionQueueTail * NVME_SQ_ENTRY_SIZE));

    // Copy command to queue
    RtlCopyMemory(sqEntry, Cmd, sizeof(NVME_COMMAND));

#ifdef NVME2K_DBG_CMD
    // Dump the command for debugging
    ScsiDebugPrint(0, "nvme2k: NvmeSubmitCommand - QID=%d Tail=%d\n", Queue->QueueId, Queue->SubmissionQueueTail);
    ScsiDebugPrint(0, "  CDW0=%08X (OPC=%02X Flags=%02X CID=%04X) NSID=%08X CDW2=%08X CDW3=%08X\n",
                   sqEntry->CDW0.AsUlong, sqEntry->CDW0.Fields.Opcode, sqEntry->CDW0.Fields.Flags,
                   sqEntry->CDW0.Fields.CommandId, sqEntry->NSID, sqEntry->CDW2, sqEntry->CDW3);
    ScsiDebugPrint(0, "  MPTR=%08X%08X\n",
                   (ULONG)(sqEntry->MPTR >> 32), (ULONG)(sqEntry->MPTR & 0xFFFFFFFF));
    ScsiDebugPrint(0, "  PRP1=%08X%08X PRP2=%08X%08X\n",
                   (ULONG)(sqEntry->PRP1 >> 32), (ULONG)(sqEntry->PRP1 & 0xFFFFFFFF),
                   (ULONG)(sqEntry->PRP2 >> 32), (ULONG)(sqEntry->PRP2 & 0xFFFFFFFF));
    ScsiDebugPrint(0, "  CDW10=%08X CDW11=%08X CDW12=%08X CDW13=%08X\n",
                   sqEntry->CDW10, sqEntry->CDW11, sqEntry->CDW12, sqEntry->CDW13);
    ScsiDebugPrint(0, "  CDW14=%08X CDW15=%08X\n",
                   sqEntry->CDW14, sqEntry->CDW15);
#endif
    // Update tail
    Queue->SubmissionQueueTail = nextTail;

    // Ring doorbell
    NvmeRingDoorbell(devExt, Queue->QueueId, TRUE, (USHORT)(Queue->SubmissionQueueTail));

#ifdef NVME2K_USE_SUBMISSION_LOCK
    // Release submission queue spinlock (atomic write to ensure memory ordering)
    AtomicSet(&Queue->SubmissionLock, 0);
#endif

    return TRUE;
}

//
// NvmeProcessAdminCompletion - Process admin queue completions
//
BOOLEAN NvmeProcessAdminCompletion(IN PHW_DEVICE_EXTENSION devExt)
{
    PNVME_QUEUE Queue = &devExt->AdminQueue;
    PNVME_COMPLETION cqEntry;
    BOOLEAN processed = FALSE;
    USHORT status;
    USHORT commandId;
    PNVME_IDENTIFY_NAMESPACE nsData;
    ULONG queueIndex;
    ULONG expectedPhase;

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Acquire completion queue spinlock - protects against concurrent interrupt handler calls
    if (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeProcessAdminCompletion - lock contention on QID=%d (spinning)\n", Queue->QueueId);
#endif
        while (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
            // Spin waiting for lock
        }
    }
#endif

    while (TRUE) {
        // Calculate queue index from current head
        queueIndex = Queue->CompletionQueueHead & Queue->QueueSizeMask;

        // Calculate expected phase from counter (toggles every QueueSize completions)
        expectedPhase = (Queue->CompletionQueueHead >> Queue->QueueSizeBits) & 1;

        // Get completion queue entry
        cqEntry = (PNVME_COMPLETION)((PUCHAR)Queue->CompletionQueue +
                                     (queueIndex * NVME_CQ_ENTRY_SIZE));

        // Check phase bit
        if ((cqEntry->Status & 1u) != expectedPhase) {
            // No more completions
            break;
        }

        processed = TRUE;

        // Extract status and command ID
        status = (cqEntry->Status >> 1) & 0xFF;
        commandId = cqEntry->CID;

        // Update submission queue head from completion entry
        Queue->SubmissionQueueHead = cqEntry->SQHead;

        // Increment completion queue head
        Queue->CompletionQueueHead++;

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeProcessAdminCompletion - CID=%d Status=0x%04X SQHead=%d\n",
                       commandId, status, Queue->SubmissionQueueHead);
#endif
        if (!devExt->InitComplete) {
            switch (commandId) {
                case ADMIN_CID_CREATE_IO_CQ:
                    if (status == NVME_SC_SUCCESS) {
                        NvmeCreateIoSQ(devExt);
                    }
                    break;

                case ADMIN_CID_CREATE_IO_SQ:
                    if (status == NVME_SC_SUCCESS) {
                        NvmeIdentifyController(devExt);
                    }
                    break;

                case ADMIN_CID_IDENTIFY_CONTROLLER:
                    if (status == NVME_SC_SUCCESS) {
                        PNVME_IDENTIFY_CONTROLLER ctrlData = (PNVME_IDENTIFY_CONTROLLER)devExt->UtilityBuffer;

                        // Copy and null-terminate strings
                        RtlCopyMemory(devExt->ControllerSerialNumber, ctrlData->SerialNumber, 20);
                        devExt->ControllerSerialNumber[20] = 0;

                        RtlCopyMemory(devExt->ControllerModelNumber, ctrlData->ModelNumber, 40);
                        devExt->ControllerModelNumber[40] = 0;

                        RtlCopyMemory(devExt->ControllerFirmwareRevision, ctrlData->FirmwareRevision, 8);
                        devExt->ControllerFirmwareRevision[8] = 0;

                        devExt->NumberOfNamespaces = ctrlData->NumberOfNamespaces;

#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: Identified controller - Model: %.40s SN: %.20s FW: %.8s NN: %u\n",
                                    devExt->ControllerModelNumber, devExt->ControllerSerialNumber,
                                    devExt->ControllerFirmwareRevision, devExt->NumberOfNamespaces);
#endif
                        NvmeIdentifyNamespace(devExt);
                    }
                    break;

                case ADMIN_CID_IDENTIFY_NAMESPACE:
                    if (status == NVME_SC_SUCCESS) {
                        ScsiDebugPrint(0, "nvme2k: Namespace identified successfully\n");

                        nsData = (PNVME_IDENTIFY_NAMESPACE)devExt->UtilityBuffer;
                        devExt->NamespaceSizeInBlocks = nsData->NamespaceSize;

                        // Extract block size from formatted LBA size
                        devExt->NamespaceBlockSize = 1 << (nsData->FormattedLbaSize & 0x0F);
                        if (devExt->NamespaceBlockSize == 1) {
                            devExt->NamespaceBlockSize = 512;  // Default
                        }

#ifdef NVME2K_DBG
                        ScsiDebugPrint(0, "nvme2k: Identified namespace - blocks=%I64u blocksize=%u bytes\n",
                                    devExt->NamespaceSizeInBlocks, devExt->NamespaceBlockSize);
#endif

                        // Initialization complete - now alias UtilityBuffer as PRP list pages
                        // UtilityBuffer was used for Identify commands, now repurpose it for SG lists
                        devExt->PrpListPages = devExt->UtilityBuffer;
                        devExt->PrpListPagesPhys = devExt->UtilityBufferPhys;
                        devExt->PrpListPageBitmap = 0;  // All pages free

                        // Initialize statistics
                        devExt->CurrentQueueDepth = 0;
                        devExt->MaxQueueDepthReached = 0;
                        devExt->CurrentPrpListPagesUsed = 0;
                        devExt->MaxPrpListPagesUsed = 0;
                        devExt->TotalRequests = 0;
                        devExt->TotalReads = 0;
                        devExt->TotalWrites = 0;
                        devExt->TotalBytesRead = 0;
                        devExt->TotalBytesWritten = 0;
                        devExt->MaxReadSize = 0;
                        devExt->MaxWriteSize = 0;
                        devExt->RejectedRequests = 0;

                        ScsiDebugPrint(0, "nvme2k: PRP list pool initialized at VA=%p PA=%08X%08X (%d pages)\n",
                                       devExt->PrpListPages,
                                       (ULONG)(devExt->PrpListPagesPhys.QuadPart >> 32),
                                       (ULONG)(devExt->PrpListPagesPhys.QuadPart & 0xFFFFFFFF),
                                       SG_LIST_PAGES);

                        devExt->InitComplete = TRUE;
                    }
                    break;

                default:
#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: unknown init time admin CID %04X\n", commandId);
#endif
            }
        } else {
            // Post-initialization admin commands
            // Check if this is a Get Log Page command
            if (commandId == ADMIN_CID_GET_LOG_PAGE) {
                PSCSI_REQUEST_BLOCK srb;
                PSRB_IO_CONTROL srbControl;
                PSENDCMDOUTPARAMS sendCmdOut;
                PVOID prpBuffer;
                PNVME_SRB_EXTENSION srbExt;
                UCHAR prpPageIndex;

                // Get the untagged SRB from ScsiPort
                // ScsiPort guarantees only one untagged request at a time
                srb = ScsiPortGetSrb(devExt, 0, 0, 0, SP_UNTAGGED);

                if (srb) {
                    // Get PRP page index from SRB extension
                    srbExt = (PNVME_SRB_EXTENSION)srb->SrbExtension;
                    prpPageIndex = srbExt ? srbExt->PrpListPage : 0xFF;

#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: Get Log Page completion - CID=%u PRP=%u Status=0x%04X\n",
                                   commandId, prpPageIndex, status);
#endif

                    // Get the PRP buffer containing the log page data
                    if (prpPageIndex != 0xFF) {
                        prpBuffer = (PVOID)((PUCHAR)devExt->PrpListPages + (prpPageIndex * PAGE_SIZE));
                    } else {
                        prpBuffer = NULL;
                    }

                    // Get output structure
                    srbControl = (PSRB_IO_CONTROL)srb->DataBuffer;
                    sendCmdOut = (PSENDCMDOUTPARAMS)((PUCHAR)srb->DataBuffer + sizeof(SRB_IO_CONTROL));

                    if (status == NVME_SC_SUCCESS && prpBuffer) {
                        PNVME_SMART_INFO nvmeSmart = (PNVME_SMART_INFO)prpBuffer;
                        PATA_SMART_DATA ataSmart = (PATA_SMART_DATA)sendCmdOut->bBuffer;

                        // Convert NVMe SMART/Health log to ATA SMART format
                        NvmeSmartToAtaSmart(nvmeSmart, ataSmart);

                        sendCmdOut->cBufferSize = 512;
                        RtlZeroMemory(&sendCmdOut->DriverStatus, sizeof(DRIVERSTATUS));
                        sendCmdOut->DriverStatus.bDriverError = 0;
                        sendCmdOut->DriverStatus.bIDEError = 0;

                        srbControl->ReturnCode = 0;
                        srb->SrbStatus = SRB_STATUS_SUCCESS;

                    } else {
                        // Command failed
                        sendCmdOut->cBufferSize = 0;
                        sendCmdOut->DriverStatus.bDriverError = 1;
                        sendCmdOut->DriverStatus.bIDEError = 0x04;  // Aborted
                        srbControl->ReturnCode = 1;
                        srb->SrbStatus = SRB_STATUS_ERROR;
                    }

                    // Free the PRP page
                    if (srbExt && srbExt->PrpListPage != 0xFF) {
                        FreePrpListPage(devExt, srbExt->PrpListPage);
                        srbExt->PrpListPage = 0xFF;
                    }

                    // Complete the SRB
                    ScsiPortNotification(RequestComplete, devExt, srb);
                    ScsiPortNotification(NextRequest, devExt, NULL);
                } else {
#ifdef NVME2K_DBG
                    ScsiDebugPrint(0, "nvme2k: Get Log Page completion - ScsiPortGetSrb returned NULL\n");
#endif
                }
            } else {
#ifdef NVME2K_DBG
                ScsiDebugPrint(0, "nvme2k: unknown admin CID %04X\n", commandId);
#endif
            }
        }

        // Ring completion doorbell with the new head position
        NvmeRingDoorbell(devExt, Queue->QueueId, FALSE, (USHORT)(Queue->CompletionQueueHead & Queue->QueueSizeMask));
    }

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Release completion queue spinlock
    AtomicSet(&Queue->CompletionLock, 0);
#endif

    return processed;
}

//
// NvmeProcessIoCompletion - Process I/O queue completions
//
BOOLEAN NvmeProcessIoCompletion(IN PHW_DEVICE_EXTENSION devExt)
{
    PNVME_QUEUE Queue = &devExt->IoQueue;
    PNVME_COMPLETION cqEntry;
    BOOLEAN processed = FALSE;
    USHORT status;
    USHORT commandId;
    PSCSI_REQUEST_BLOCK srb;
    ULONG queueIndex;
    ULONG expectedPhase;

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Acquire completion queue spinlock - protects against concurrent interrupt handler calls
    if (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeProcessIoCompletion - lock contention on QID=%d (spinning)\n", Queue->QueueId);
#endif
        while (!AtomicCompareExchange(&Queue->CompletionLock, 1, 0)) {
            // Spin waiting for lock
        }
    }
#endif

    while (TRUE) {
        // Calculate queue index from current head
        queueIndex = Queue->CompletionQueueHead & Queue->QueueSizeMask;

        // Calculate expected phase from counter (toggles every QueueSize completions)
        expectedPhase = (Queue->CompletionQueueHead >> Queue->QueueSizeBits) & 1;

        // Get completion queue entry
        cqEntry = (PNVME_COMPLETION)((PUCHAR)Queue->CompletionQueue +
                                     (queueIndex * NVME_CQ_ENTRY_SIZE));

        // Check phase bit
        if ((cqEntry->Status & 1u) != expectedPhase) {
            // No more completions
            break;
        }

        processed = TRUE;

        // Extract status and command ID
        status = (cqEntry->Status >> 1) & 0xFF;
        commandId = cqEntry->CID;

        // Update submission queue head from completion entry
        Queue->SubmissionQueueHead = cqEntry->SQHead;

        // Increment completion queue head
        Queue->CompletionQueueHead++;

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeProcessIoCompletion - CID=%d Status=0x%04X SQHead=%d\n",
                       commandId, status, Queue->SubmissionQueueHead);
#endif
        // Retrieve SRB from command ID using ScsiPortGetSrb
        srb = NvmeGetSrbFromCommandId(devExt, commandId);

        if (srb == NULL) {
            ScsiDebugPrint(0, "nvme2k: ERROR - Got NULL SRB for CID=%d! This should not happen.\n", commandId);
            continue;
        }

        {
            PNVME_SRB_EXTENSION srbExt;

            // Validate SRB before processing
            if (srb->SrbStatus != SRB_STATUS_PENDING) {
                ScsiDebugPrint(0, "nvme2k: ERROR - SRB CID=%d has invalid status 0x%02X (expected PENDING=0x%02X)\n",
                               commandId, srb->SrbStatus, SRB_STATUS_PENDING);
                ScsiDebugPrint(0, "nvme2k:        This suggests DOUBLE COMPLETION! SRB=%p\n", srb);
                // Skip this - it's already been completed
                continue;
            }

            if (srb->Function != SRB_FUNCTION_EXECUTE_SCSI) {
                ScsiDebugPrint(0, "nvme2k: ERROR - SRB CID=%d has invalid function 0x%02X\n",
                               commandId, srb->Function);
            }

            if (!srb->SrbExtension) {
                ScsiDebugPrint(0, "nvme2k: ERROR - SRB CID=%d has NULL SrbExtension!\n", commandId);
                // Skip this completion
                continue;
            }

            // Get SRB extension for PRP list cleanup
            srbExt = (PNVME_SRB_EXTENSION)srb->SrbExtension;

            // Free PRP list page if allocated
            if (srbExt->PrpListPage != 0xFF) {
                FreePrpListPage(devExt, srbExt->PrpListPage);
                srbExt->PrpListPage = 0xFF;
            }

            // Check if this was a non-tagged request and clear the flag
            if (commandId & CID_NON_TAGGED_FLAG) {
                devExt->NonTaggedInFlight = FALSE;
            }

            // Set SRB status based on NVMe status
            if (status == NVME_SC_SUCCESS) {
                srb->SrbStatus = SRB_STATUS_SUCCESS;
#ifdef NVME_DBG_EXTRA
                // to spammy enable by default
                ScsiDebugPrint(0, "nvme2k: Completing CID=%d SRB=%p SUCCESS\n", commandId, srb);
#endif
            } else {
                // Command failed - provide auto-sense data
                srb->SrbStatus = SRB_STATUS_ERROR;
                srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

                // Fill in sense data if buffer is available
                if (srb->SenseInfoBuffer && srb->SenseInfoBufferLength >= 18) {
                    PUCHAR sense = (PUCHAR)srb->SenseInfoBuffer;
                    RtlZeroMemory(sense, srb->SenseInfoBufferLength);

                    // Build standard SCSI sense data
                    sense[0] = 0x70;  // Error code: Current error
                    sense[2] = 0x04;  // Sense Key: Hardware Error
                    sense[7] = 0x0A;  // Additional sense length
                    sense[12] = 0x44; // ASC: Internal target failure
                    sense[13] = 0x00; // ASCQ

                    srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                }

                ScsiDebugPrint(0, "nvme2k: I/O command failed - CID=%d NVMe Status=0x%02X\n",
                               commandId, status);
            }

            // Complete the request - ScsiPort takes ownership of the SRB
            ScsiPortNotification(RequestComplete, devExt, srb);
        }

        // Ring completion doorbell with the new head position
        NvmeRingDoorbell(devExt, Queue->QueueId, FALSE, (USHORT)(Queue->CompletionQueueHead & Queue->QueueSizeMask));
    }

#ifdef NVME2K_USE_COMPLETION_LOCK
    // Release completion queue spinlock
    AtomicSet(&Queue->CompletionLock, 0);
#endif

    return processed;
}

BOOLEAN NvmeCreateIoCQ(IN PHW_DEVICE_EXTENSION devExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_CREATE_CQ;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_CREATE_IO_CQ;
    cmd.CDW10 = ((devExt->IoQueue.QueueSize - 1) << 16) | devExt->IoQueue.QueueId;
    // CDW11: PC=1 (bit 0), IEN=1 (bit 1) to enable interrupts, IV=0 (bits 31:16)
    cmd.CDW11 = NVME_QUEUE_PHYS_CONTIG | NVME_QUEUE_IRQ_ENABLED | (0 << 16);
    cmd.PRP1 = devExt->IoQueue.CompletionQueuePhys.QuadPart;

    return NvmeSubmitCommand(devExt, &devExt->AdminQueue, &cmd);
}

BOOLEAN NvmeCreateIoSQ(IN PHW_DEVICE_EXTENSION devExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_CREATE_SQ;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_CREATE_IO_SQ;
    cmd.CDW10 = ((devExt->IoQueue.QueueSize - 1) << 16) | devExt->IoQueue.QueueId;
    cmd.CDW11 = NVME_QUEUE_PHYS_CONTIG | (devExt->IoQueue.QueueId << 16);  // CQID
    cmd.PRP1 = devExt->IoQueue.SubmissionQueuePhys.QuadPart;

    return NvmeSubmitCommand(devExt, &devExt->AdminQueue, &cmd);
}

//
// NvmeIdentifyController - Send Identify Controller command
//
BOOLEAN NvmeIdentifyController(IN PHW_DEVICE_EXTENSION devExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    // Build Identify Controller command
    cmd.CDW0.Fields.Opcode = NVME_ADMIN_IDENTIFY;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_IDENTIFY_CONTROLLER;
    cmd.NSID = 0;  // Not used for controller identify
    cmd.PRP1 = devExt->UtilityBufferPhys.QuadPart;
    cmd.PRP2 = 0;  // Single page transfer, no PRP2 needed
    cmd.CDW10 = NVME_CNS_CONTROLLER;

#ifdef NVME2K_DBG_CMD
    ScsiDebugPrint(0, "nvme2k: NvmeIdentifyController - CDW0=%08X (OPC=%02X CID=%04X) NSID=%08X PRP1=%08X%08X CDW10=%08X\n",
                   cmd.CDW0.AsUlong, cmd.CDW0.Fields.Opcode, cmd.CDW0.Fields.CommandId,
                   cmd.NSID, (ULONG)(cmd.PRP1 >> 32), (ULONG)(cmd.PRP1 & 0xFFFFFFFF),
                   cmd.CDW10);
#endif
    return NvmeSubmitCommand(devExt, &devExt->AdminQueue, &cmd);
}

//
// NvmeIdentifyNamespace - Send Identify Namespace command
//
BOOLEAN NvmeIdentifyNamespace(IN PHW_DEVICE_EXTENSION devExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    // Build Identify Namespace command
    cmd.CDW0.Fields.Opcode = NVME_ADMIN_IDENTIFY;
    cmd.CDW0.Fields.Flags = 0;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_IDENTIFY_NAMESPACE;
    cmd.NSID = 1;  // Namespace ID 1
    cmd.PRP1 = devExt->UtilityBufferPhys.QuadPart;
    cmd.PRP2 = 0;  // Single page transfer, no PRP2 needed
    cmd.CDW10 = NVME_CNS_NAMESPACE;

#ifdef NVME2K_DBG_CMD
    ScsiDebugPrint(0, "nvme2k: NvmeIdentifyNamespace - CDW0=%08X (OPC=%02X CID=%04X) NSID=%08X PRP1=%08X%08X CDW10=%08X\n",
                   cmd.CDW0.AsUlong, cmd.CDW0.Fields.Opcode, cmd.CDW0.Fields.CommandId,
                   cmd.NSID, (ULONG)(cmd.PRP1 >> 32), (ULONG)(cmd.PRP1 & 0xFFFFFFFF),
                   cmd.CDW10);
#endif
    return NvmeSubmitCommand(devExt, &devExt->AdminQueue, &cmd);
}

//
// NvmeGetLogPage - Retrieve a log page from NVMe device asynchronously
// Uses PRP page allocator for DMA buffer, untagged operation
// ScsiPort guarantees only one untagged request at a time
//
BOOLEAN NvmeGetLogPage(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR LogPageId)
{
    NVME_COMMAND cmd;
    PHYSICAL_ADDRESS physAddr;
    UCHAR prpPageIndex;
    ULONG numdl;
    PNVME_SRB_EXTENSION srbExt;

    // Allocate a PRP page for the log data buffer (4KB)
    prpPageIndex = AllocatePrpListPage(devExt);
    if (prpPageIndex == 0xFF) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeGetLogPage - Failed to allocate PRP page\n");
#endif
        return FALSE;
    }

    // Get physical address of the PRP buffer
    physAddr = devExt->PrpListPagesPhys;
    physAddr.QuadPart += (prpPageIndex * PAGE_SIZE);

    // Store PRP page index in SRB extension so we can free it on completion
    if (!Srb->SrbExtension) {
#ifdef NVME2K_DBG
        ScsiDebugPrint(0, "nvme2k: NvmeGetLogPage - SRB has no extension\n");
#endif
        FreePrpListPage(devExt, prpPageIndex);
        return FALSE;
    }

    srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    srbExt->PrpListPage = prpPageIndex;

    // Calculate NUMDL (number of dwords - 1) for 512 bytes (SMART log size)
    numdl = (512 / 4) - 1;

    RtlZeroMemory(&cmd, sizeof(NVME_COMMAND));

    cmd.CDW0.Fields.Opcode = NVME_ADMIN_GET_LOG_PAGE;
    cmd.CDW0.Fields.CommandId = ADMIN_CID_GET_LOG_PAGE;
    cmd.NSID = 0xFFFFFFFF;  // Global log page (not namespace-specific)
    cmd.PRP1 = (ULONGLONG)physAddr.LowPart | ((ULONGLONG)physAddr.HighPart << 32);
    cmd.PRP2 = 0;  // Single page transfer

    // CDW10: NUMDL (bits 15:0) and LID (bits 7:0 in upper word)
    cmd.CDW10 = (numdl & 0xFFFF) | ((ULONG)LogPageId << 16);

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: NvmeGetLogPage - LID=0x%02X PRP=%u Phys=%08X%08X\n",
                   LogPageId, prpPageIndex, physAddr.HighPart, physAddr.LowPart);
#endif

    // ScsiPort manages the untagged SRB, we can retrieve it in completion with ScsiPortGetSrb()

    return NvmeSubmitCommand(devExt, &devExt->AdminQueue, &cmd);
}

//
// NvmeBuildCommandId - Build NVMe Command ID from SRB
// For tagged requests: Use QueueTag directly (bit 15 clear)
// For non-tagged requests: Generate sequence number with bit 15 set
//
USHORT NvmeBuildCommandId(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    USHORT commandId;

    // Check if this is a tagged request
    // Note: QueueTag == SP_UNTAGGED (0xFF) means non-tagged even if SRB_FLAGS_QUEUE_ACTION_ENABLE is set
    if ((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb->QueueTag != SP_UNTAGGED)) {
        // Tagged request - use QueueTag directly
        commandId = (USHORT)Srb->QueueTag & CID_VALUE_MASK;
    } else {
        // Non-tagged request - generate sequence number with flag bit set
        commandId = (devExt->NextNonTaggedId & CID_VALUE_MASK) | CID_NON_TAGGED_FLAG;
        devExt->NextNonTaggedId++;
        // Wrap around at 15 bits
        if (devExt->NextNonTaggedId >= 0x8000) {
            devExt->NextNonTaggedId = 0;
        }
    }

    return commandId;
}

//
// NvmeBuildFlushCommandId - Build CID for ORDERED tag flush command
// Uses the SRB's QueueTag with the ORDERED_FLUSH flag bit set
//
USHORT NvmeBuildFlushCommandId(IN PSCSI_REQUEST_BLOCK Srb)
{
    USHORT commandId;

    // For ORDERED flush, use QueueTag with flush flag bit set
    commandId = ((USHORT)Srb->QueueTag & CID_VALUE_MASK) | CID_ORDERED_FLUSH_FLAG;

    return commandId;
}

//
// NvmeGetSrbFromCommandId - Retrieve SRB from Command ID
// Decodes the CID and calls ScsiPortGetSrb with appropriate parameters
//
PSCSI_REQUEST_BLOCK NvmeGetSrbFromCommandId(IN PHW_DEVICE_EXTENSION devExt, IN USHORT commandId)
{
    LONG queueTag;

    // Check if this is a non-tagged request (bit 15 set)
    if (commandId & CID_NON_TAGGED_FLAG) {
        // Non-tagged request - use SP_UNTAGGED
        queueTag = SP_UNTAGGED;
    } else if (commandId & CID_ORDERED_FLUSH_FLAG) {
        // There is no actual SRB for the flush, the next command has SRB
        return NULL;
    } else {
        // Normal tagged request - extract QueueTag
        queueTag = (LONG)(commandId & CID_VALUE_MASK);
    }

    // Call ScsiPortGetSrb to retrieve the SRB
    // PathId=0, TargetId=0, Lun=0 for our single device
    return ScsiPortGetSrb(devExt, 0, 0, 0, queueTag);
}

//
// NvmeBuildReadWriteCommand - Build NVMe Read/Write command from SCSI CDB
//
VOID NvmeBuildReadWriteCommand(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb, IN PNVME_COMMAND Cmd, IN USHORT CommandId)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    ULONGLONG lba = 0;
    ULONG numBlocks = 0;
    BOOLEAN isWrite = FALSE;
    PHYSICAL_ADDRESS physAddr;
    PHYSICAL_ADDRESS physAddr2;
    ULONG length;
    ULONG pageSize = 4096;
    ULONG offsetInPage;
    ULONG firstPageBytes;
    PVOID currentPageVirtual;
    ULONG remainingBytes;
    ULONG currentOffset;
    UCHAR prpListPage;
    PULONGLONG prpList;
    PHYSICAL_ADDRESS prpListPhys;
    ULONG prpIndex;
    ULONG numPrpEntries;
    PNVME_SRB_EXTENSION srbExt;

    // Initialize SRB extension
    srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    srbExt->PrpListPage = 0xFF;  // No PRP list initially

    // Parse CDB based on opcode
    switch (cdb->CDB10.OperationCode) {
        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
            lba = ((ULONG)(cdb->CDB6READWRITE.LogicalBlockMsb1) << 16) |
                  ((ULONG)(cdb->CDB6READWRITE.LogicalBlockMsb0) << 8) |
                  ((ULONG)(cdb->CDB6READWRITE.LogicalBlockLsb));
            numBlocks = cdb->CDB6READWRITE.TransferBlocks;
            if (numBlocks == 0) {
                numBlocks = 256;  // 0 means 256 blocks in READ(6)/WRITE(6)
            }
            isWrite = (cdb->CDB6READWRITE.OperationCode == SCSIOP_WRITE6);
            break;

        case SCSIOP_READ:
        case SCSIOP_WRITE:
            lba = ((ULONG)cdb->CDB10.LogicalBlockByte0 << 24) |
                  ((ULONG)cdb->CDB10.LogicalBlockByte1 << 16) |
                  ((ULONG)cdb->CDB10.LogicalBlockByte2 << 8) |
                  ((ULONG)cdb->CDB10.LogicalBlockByte3);
            numBlocks = ((ULONG)cdb->CDB10.TransferBlocksMsb << 8) |
                        ((ULONG)cdb->CDB10.TransferBlocksLsb);
            isWrite = (cdb->CDB10.OperationCode == SCSIOP_WRITE);
            break;
    }

    // Build NVMe command
    if (isWrite) {
        Cmd->CDW0.Fields.Opcode = NVME_CMD_WRITE;
    } else {
        Cmd->CDW0.Fields.Opcode = NVME_CMD_READ;
    }
    Cmd->CDW0.Fields.Flags = NVME_CMD_PRP;
    Cmd->CDW0.Fields.CommandId = CommandId;

    Cmd->NSID = 1;  // Namespace ID 1

    // Track I/O statistics
    devExt->TotalRequests++;
    if (isWrite) {
        devExt->TotalWrites++;
        devExt->TotalBytesWritten += Srb->DataTransferLength;
        if (Srb->DataTransferLength > devExt->MaxWriteSize) {
            devExt->MaxWriteSize = Srb->DataTransferLength;
        }
    } else {
        devExt->TotalReads++;
        devExt->TotalBytesRead += Srb->DataTransferLength;
        if (Srb->DataTransferLength > devExt->MaxReadSize) {
            devExt->MaxReadSize = Srb->DataTransferLength;
        }
    }
#if NVME2K_DBG_STATS
    // Print statistics every 10000 requests
    // Note: QDepth tracking removed (always 0) since we no longer store SRBs
    if ((devExt->TotalRequests % 10000) == 0) {
        ScsiDebugPrint(0, "nvme2k: Stats - Reqs=%u R=%u W=%u BytesR=%I64u BytesW=%I64u MaxR=%u MaxW=%u PRP=%u/%u Rejected=%u\n",
                       devExt->TotalRequests,
                       devExt->TotalReads,
                       devExt->TotalWrites,
                       devExt->TotalBytesRead,
                       devExt->TotalBytesWritten,
                       devExt->MaxReadSize,
                       devExt->MaxWriteSize,
                       devExt->CurrentPrpListPagesUsed,
                       devExt->MaxPrpListPagesUsed,
                       devExt->RejectedRequests);
    }
#endif
    // Get physical address of data buffer
    length = Srb->DataTransferLength;
    physAddr = ScsiPortGetPhysicalAddress(devExt, Srb, Srb->DataBuffer, &length);

#ifdef NVME2K_DBG_CMD
    ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - DataBuffer=%p TransferLen=%u PhysAddr=%08X%08X ReturnedLen=%u\n",
                   Srb->DataBuffer, Srb->DataTransferLength,
                   (ULONG)(physAddr.QuadPart >> 32), (ULONG)(physAddr.QuadPart & 0xFFFFFFFF),
                   length);
#endif
    // Set PRP1 to the start of the data
    Cmd->PRP1 = physAddr.QuadPart;

    // Calculate offset within the page
    offsetInPage = (ULONG)(physAddr.QuadPart & (PAGE_SIZE - 1));

    // Calculate how many bytes fit in the first page
    firstPageBytes = PAGE_SIZE - offsetInPage;

    // Determine if we need PRP2 or a PRP list
    if (Srb->DataTransferLength <= firstPageBytes) {
        // Transfer fits in one page
        Cmd->PRP2 = 0;
#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - Single page transfer, PRP2=0\n");
#endif
    } else if (Srb->DataTransferLength <= (firstPageBytes + PAGE_SIZE)) {
        // Transfer spans exactly 2 pages, use PRP2 directly
        currentPageVirtual = (PVOID)((PUCHAR)Srb->DataBuffer + firstPageBytes);
        length = Srb->DataTransferLength - firstPageBytes;
        physAddr2 = ScsiPortGetPhysicalAddress(devExt, Srb, currentPageVirtual, &length);

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - Two page transfer: PhysAddr2=%08X%08X\n",
                       (ULONG)(physAddr2.QuadPart >> 32), (ULONG)(physAddr2.QuadPart & 0xFFFFFFFF));
#endif
        Cmd->PRP2 = physAddr2.QuadPart;
    } else {
        // Transfer spans more than 2 pages, need PRP list
        prpListPage = AllocatePrpListPage(devExt);
        if (prpListPage == 0xFF) {
            // No PRP list pages available - this shouldn't happen if we sized correctly
            ScsiDebugPrint(0, "nvme2k: ERROR - No PRP list pages available!\n");
            Cmd->PRP2 = 0;
            return;
        }

        // Store PRP list page in SRB extension
        {
            PNVME_SRB_EXTENSION srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
            srbExt->PrpListPage = prpListPage;
        }

        // Get virtual and physical addresses of PRP list
        prpList = (PULONGLONG)GetPrpListPageVirtual(devExt, prpListPage);
        prpListPhys = GetPrpListPagePhysical(devExt, prpListPage);

        // Build PRP list for remaining pages
        remainingBytes = Srb->DataTransferLength - firstPageBytes;
        currentOffset = firstPageBytes;
        prpIndex = 0;

        while (remainingBytes > 0 && prpIndex < 512) {
            currentPageVirtual = (PVOID)((PUCHAR)Srb->DataBuffer + currentOffset);
            length = remainingBytes;
            physAddr2 = ScsiPortGetPhysicalAddress(devExt, Srb, currentPageVirtual, &length);

            prpList[prpIndex] = physAddr2.QuadPart;
            prpIndex++;

            if (remainingBytes <= PAGE_SIZE) {
                break;
            }

            remainingBytes -= PAGE_SIZE;
            currentOffset += PAGE_SIZE;
        }

        numPrpEntries = prpIndex;

#ifdef NVME2K_DBG_CMD
        ScsiDebugPrint(0, "nvme2k: NvmeBuildReadWriteCommand - PRP list: page=%u entries=%u listPhys=%08X%08X\n",
                       prpListPage, numPrpEntries,
                       (ULONG)(prpListPhys.QuadPart >> 32), (ULONG)(prpListPhys.QuadPart & 0xFFFFFFFF));
#endif

        // Set PRP2 to point to the PRP list
        Cmd->PRP2 = prpListPhys.QuadPart;
    }

    // Set LBA and number of blocks (0-based, so subtract 1)
    Cmd->CDW10 = (ULONG)(lba & 0xFFFFFFFF);
    Cmd->CDW11 = (ULONG)(lba >> 32);
    Cmd->CDW12 = (numBlocks > 0) ? (numBlocks - 1) : 0;
    Cmd->CDW13 = 0;
    Cmd->CDW14 = 0;
    Cmd->CDW15 = 0;
}

//
// ScsiHandleInquiry - Handle SCSI INQUIRY command
//
VOID ScsiHandleInquiry(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    PUCHAR inquiryData;
    ULONG i, j;

    if (Srb->DataTransferLength < 5) {
        Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
        return;
    }

    inquiryData = (PUCHAR)Srb->DataBuffer;
    RtlZeroMemory(inquiryData, Srb->DataTransferLength);

    // Check for EVPD (Enable Vital Product Data)
    // In CDB6INQUIRY, bit 0 of PageCode field is EVPD
    if (cdb->CDB6INQUIRY.PageCode & 0x01) {
        // Return error for VPD pages (not implemented)
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return;
    }

    // Standard INQUIRY data
    if (Srb->DataTransferLength >= 36) {
        inquiryData[0] = 0x00;  // Peripheral Device Type: Direct access block device
        inquiryData[1] = 0x00;  // RMB = 0 (not removable)
        inquiryData[2] = 0x05;  // Version: SPC-3
        inquiryData[3] = 0x02;  // Response Data Format
        inquiryData[4] = 0x1F;  // Additional Length (31 bytes following)
        inquiryData[5] = 0x00;  // SCCS = 0
        inquiryData[6] = 0x00;  // No special features
        inquiryData[7] = 0x02;  // CmdQue = 1 (supports tagged command queuing)

        // Vendor Identification (8 bytes) - Extract first 8 chars from model number
        // Skip leading spaces in model number
        i = 0;
        while (i < 40 && devExt->ControllerModelNumber[i] == ' ') {
            i++;
        }

        // Copy up to 8 characters for vendor field
        for (j = 0; j < 8 && i < 40 && devExt->ControllerModelNumber[i] != 0; j++, i++) {
            inquiryData[8 + j] = devExt->ControllerModelNumber[i];
        }
        // Pad with spaces if needed
        while (j < 8) {
            inquiryData[8 + j] = ' ';
            j++;
        }

        // Product Identification (16 bytes) - Continue from model number
        for (j = 0; j < 16 && i < 40 && devExt->ControllerModelNumber[i] != 0; j++, i++) {
            inquiryData[16 + j] = devExt->ControllerModelNumber[i];
        }
        // Pad with spaces if needed
        while (j < 16) {
            inquiryData[16 + j] = ' ';
            j++;
        }

        // Product Revision Level (4 bytes) - Use firmware revision
        for (j = 0; j < 4 && j < 8 && devExt->ControllerFirmwareRevision[j] != 0; j++) {
            inquiryData[32 + j] = devExt->ControllerFirmwareRevision[j];
        }
        // Pad with spaces if needed
        while (j < 4) {
            inquiryData[32 + j] = ' ';
            j++;
        }

        Srb->DataTransferLength = 36;
    }

    Srb->SrbStatus = SRB_STATUS_SUCCESS;
}

//
// ScsiHandleReadCapacity - Handle SCSI READ CAPACITY(10) command
//
VOID ScsiHandleReadCapacity(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PULONG capacityData;
    ULONG lastLba;
    ULONG blockSize;
    
    if (Srb->DataTransferLength < 8) {
        Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
        return;
    }
    
    capacityData = (PULONG)Srb->DataBuffer;
    
    // Check if namespace has been identified
    if (devExt->NamespaceSizeInBlocks == 0) {
        // Return default values
        lastLba = 0xFFFFFFFF;
        blockSize = 512;
    } else {
        // Check if capacity exceeds 32-bit
        if (devExt->NamespaceSizeInBlocks > 0xFFFFFFFF) {
            lastLba = 0xFFFFFFFF;  // Indicate to use READ CAPACITY(16)
        } else {
            lastLba = (ULONG)(devExt->NamespaceSizeInBlocks - 1);
        }
        blockSize = devExt->NamespaceBlockSize;
    }
    
    // Return in big-endian format
    capacityData[0] = ((lastLba & 0xFF) << 24) |
                      ((lastLba & 0xFF00) << 8) |
                      ((lastLba & 0xFF0000) >> 8) |
                      ((lastLba & 0xFF000000) >> 24);
    
    capacityData[1] = ((blockSize & 0xFF) << 24) |
                      ((blockSize & 0xFF00) << 8) |
                      ((blockSize & 0xFF0000) >> 8) |
                      ((blockSize & 0xFF000000) >> 24);
    
    Srb->DataTransferLength = 8;
    Srb->SrbStatus = SRB_STATUS_SUCCESS;
}

//
// ScsiHandleReadWrite - Handle SCSI READ/WRITE commands
//
VOID ScsiHandleReadWrite(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    NVME_COMMAND nvmeCmd;
    USHORT commandId;
    PNVME_SRB_EXTENSION srbExt;

    // Check if namespace is identified. If not, the device is not ready for I/O.
    if (devExt->NamespaceSizeInBlocks == 0) {
        Srb->SrbStatus = SRB_STATUS_BUSY;
        ScsiPortNotification(RequestComplete, devExt, Srb);
        ScsiPortNotification(NextRequest, devExt, NULL);
        return;
    }

    // Check if this is a non-tagged request (QueueTag == SP_UNTAGGED or no queue action enabled)
    if (!((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb->QueueTag != SP_UNTAGGED))) {
        // Non-tagged request - only one can be in flight at a time
        if (devExt->NonTaggedInFlight) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Non-tagged request rejected - another non-tagged request in flight\n");
#endif
            Srb->SrbStatus = SRB_STATUS_BUSY;
            ScsiPortNotification(RequestComplete, devExt, Srb);
            ScsiPortNotification(NextRequest, devExt, NULL);
            return;
        }
        // Mark that we now have a non-tagged request in flight
        devExt->NonTaggedInFlight = TRUE;
    }

#ifdef NVME2K_DBG_EXTRA
    // Log HEAD_OF_QUEUE tags (treated as SIMPLE since NVMe has no priority queuing)
    if ((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) &&
        (Srb->QueueAction == SRB_HEAD_OF_QUEUE_TAG_REQUEST)) {
        ScsiDebugPrint(0, "nvme2k: HEAD_OF_QUEUE tag - treating as SIMPLE (no HW priority support)\n");
    }
#endif

    // For ORDERED tags, submit a Flush command first to drain all prior operations
    if ((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) &&
        (Srb->QueueAction == SRB_ORDERED_QUEUE_TAG_REQUEST)) {
        NVME_COMMAND flushCmd;
        USHORT flushCommandId;

#ifdef NVME2K_DBG_EXTRA
        ScsiDebugPrint(0, "nvme2k: ORDERED tag on I/O - submitting Flush before I/O\n");
#endif
        // Build flush command with special ORDERED flush CID
        flushCommandId = NvmeBuildFlushCommandId(Srb);

        RtlZeroMemory(&flushCmd, sizeof(NVME_COMMAND));
        flushCmd.CDW0.Fields.Opcode = NVME_CMD_FLUSH;
        flushCmd.CDW0.Fields.Flags = 0;
        flushCmd.CDW0.Fields.CommandId = flushCommandId;
        flushCmd.NSID = 1;

#ifdef NVME2K_DBG_EXTRA
        ScsiDebugPrint(0, "nvme2k: Submitting Flush (CID=%d) before ORDERED I/O\n", flushCommandId);
#endif
        if (!NvmeSubmitCommand(devExt, &devExt->IoQueue, &flushCmd)) {
            // Flush submission failed
            Srb->SrbStatus = SRB_STATUS_BUSY;
            ScsiPortNotification(RequestComplete, devExt, Srb);
            ScsiPortNotification(NextRequest, devExt, NULL);
            return;
        }
    }

    // Build command ID for the I/O command
    commandId = NvmeBuildCommandId(devExt, Srb);

    // Initialize SRB extension
    srbExt = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    srbExt->PrpListPage = 0xFF;  // No PRP list initially

    // Build the NVMe Read/Write command from the SCSI CDB.
    RtlZeroMemory(&nvmeCmd, sizeof(NVME_COMMAND));
    NvmeBuildReadWriteCommand(devExt, Srb, &nvmeCmd, commandId);

    // Submit the command to the I/O queue.
    if (NvmeSubmitCommand(devExt, &devExt->IoQueue, &nvmeCmd)) {
        // Command submitted successfully, mark SRB as pending.
        Srb->SrbStatus = SRB_STATUS_PENDING;
        // Tell ScsiPort we can accept another request for this LU
        ScsiPortNotification(NextLuRequest, devExt, 0, 0, 0);
    } else {
        // Submission failed, likely a full queue. Free resources and mark as busy.
        if (srbExt->PrpListPage != 0xFF) {
            FreePrpListPage(devExt, srbExt->PrpListPage);
            srbExt->PrpListPage = 0xFF;
        }
        Srb->SrbStatus = SRB_STATUS_BUSY;
        ScsiPortNotification(RequestComplete, devExt, Srb);
        ScsiPortNotification(NextRequest, devExt, NULL);
    }
}

//
// ScsiHandleFlush - Handle SCSI SYNCHRONIZE_CACHE command by sending NVMe Flush
//
VOID ScsiHandleFlush(IN PHW_DEVICE_EXTENSION devExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    NVME_COMMAND nvmeCmd;
    USHORT commandId;

    // Check if namespace is identified
    if (devExt->NamespaceSizeInBlocks == 0) {
        Srb->SrbStatus = SRB_STATUS_BUSY;
        ScsiPortNotification(RequestComplete, devExt, Srb);
        ScsiPortNotification(NextRequest, devExt, NULL);
        return;
    }

    // Check if this is a non-tagged request (QueueTag == SP_UNTAGGED or no queue action enabled)
    if (!((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && (Srb->QueueTag != SP_UNTAGGED))) {
        // Non-tagged request - only one can be in flight at a time
        if (devExt->NonTaggedInFlight) {
#ifdef NVME2K_DBG
            ScsiDebugPrint(0, "nvme2k: Non-tagged flush rejected - another non-tagged request in flight\n");
#endif
            Srb->SrbStatus = SRB_STATUS_BUSY;
            ScsiPortNotification(RequestComplete, devExt, Srb);
            ScsiPortNotification(NextRequest, devExt, NULL);
            return;
        }
        // Mark that we now have a non-tagged request in flight
        devExt->NonTaggedInFlight = TRUE;
    }

    // Build command ID (flushes from SYNCHRONIZE_CACHE are standalone, not ORDERED tag flushes)
    commandId = NvmeBuildCommandId(devExt, Srb);

    // Build NVMe Flush command
    RtlZeroMemory(&nvmeCmd, sizeof(NVME_COMMAND));
    nvmeCmd.CDW0.Fields.Opcode = NVME_CMD_FLUSH;
    nvmeCmd.CDW0.Fields.Flags = 0;
    nvmeCmd.CDW0.Fields.CommandId = commandId;
    nvmeCmd.NSID = 1;  // Namespace ID 1

    // Submit the Flush command
    if (NvmeSubmitCommand(devExt, &devExt->IoQueue, &nvmeCmd)) {
        Srb->SrbStatus = SRB_STATUS_PENDING;
        // Tell ScsiPort we can accept another request for this LU
        ScsiPortNotification(NextLuRequest, devExt, 0, 0, 0);
    } else {
        // Submission failed
        Srb->SrbStatus = SRB_STATUS_BUSY;
        ScsiPortNotification(RequestComplete, devExt, Srb);
        ScsiPortNotification(NextRequest, devExt, NULL);
    }
}
