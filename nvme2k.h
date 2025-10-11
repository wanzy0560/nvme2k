//
// NVMe Driver for Windows 2000 - Header File
// Contains all defines, constants, and type definitions
//

#ifndef _NVME2K_H_
#define _NVME2K_H_

#include <miniport.h>
#include <scsi.h>
#include "atomic.h"
#include "utils.h"

#define NVME2K_DBG
// extra spammy logging for NVMe commands
// #define NVME2K_DBG_CMD
// uncomment to enable debugging and logging
/* for debug messages use
 * ScsiDebugPrint(0, "nvme2k: ...\n");
 * it seems only level 0 messages are displayed, possibly registry changes are needed
 * #define NVME2K_DBG_STATS
 */

//
// Synchronization control - comment out to disable specific locks
//
//#define NVME2K_USE_INTERRUPT_LOCK       // Lock to serialize HwInterrupt on SMP
//#define NVME2K_USE_SUBMISSION_LOCK      // Lock to serialize command submission
//#define NVME2K_USE_COMPLETION_LOCK      // Lock to serialize completion processing

//
// Memory constants
//
#define PAGE_SIZE                           0x1000  // 4KB page size

//
// Uncached memory size calculation:
// - Admin SQ: 4096 bytes (4KB aligned)
// - I/O SQ: 4096 bytes (4KB aligned)
// - Utility buffer / PRP list pool: 40960 bytes (10 pages * 4KB, page-aligned)
// - Admin CQ: 4096 bytes (4KB aligned)
// - I/O CQ: 4096 bytes (4KB aligned)
// Total: ~60KB with alignment
//
#define UNCACHED_EXTENSION_SIZE             (PAGE_SIZE * 16)  // 64KB for safety

//
// NVMe PCI Class Codes
//
#define PCI_CLASS_MASS_STORAGE_CONTROLLER   0x01
#define PCI_SUBCLASS_NON_VOLATILE_MEMORY    0x08
#define PCI_PROGIF_NVME                     0x02

//
// PCI Configuration Space Offsets
//
#define PCI_VENDOR_ID_OFFSET                0x00
#define PCI_DEVICE_ID_OFFSET                0x02
#define PCI_COMMAND_OFFSET                  0x04
#define PCI_STATUS_OFFSET                   0x06
#define PCI_REVISION_ID_OFFSET              0x08
#define PCI_CLASS_CODE_OFFSET               0x09
#define PCI_HEADER_TYPE_OFFSET              0x0E
#define PCI_BASE_ADDRESS_0                  0x10
#define PCI_SUBSYSTEM_VENDOR_ID_OFFSET      0x2C
#define PCI_SUBSYSTEM_ID_OFFSET             0x2E

//
// PCI Command Register bits
//
#define PCI_ENABLE_IO_SPACE                 0x0001
#define PCI_ENABLE_MEMORY_SPACE             0x0002
#define PCI_ENABLE_BUS_MASTER               0x0004

//
// NVMe Controller Registers (offset from BAR0)
//
#define NVME_REG_CAP        0x00    // Controller Capabilities
#define NVME_REG_VS         0x08    // Version
#define NVME_REG_INTMS      0x0C    // Interrupt Mask Set
#define NVME_REG_INTMC      0x10    // Interrupt Mask Clear
#define NVME_REG_CC         0x14    // Controller Configuration
#define NVME_REG_CSTS       0x1C    // Controller Status
#define NVME_REG_AQA        0x24    // Admin Queue Attributes
#define NVME_REG_ASQ        0x28    // Admin Submission Queue Base Address
#define NVME_REG_ACQ        0x30    // Admin Completion Queue Base Address

//
// Doorbell registers (stride determined by CAP.DSTRD)
//
#define NVME_REG_DBS        0x1000  // Doorbell base

//
// Controller Capabilities Register bits
//
#define NVME_CAP_MQES_MASK  0x0000FFFF  // Maximum Queue Entries Supported (bits 15:0)

//
// Controller Configuration Register bits
//
#define NVME_CC_ENABLE      0x00000001
#define NVME_CC_CSS_NVM     0x00000000
#define NVME_CC_MPS_SHIFT   7
#define NVME_CC_AMS_RR      0x00000000
#define NVME_CC_SHN_NONE    0x00000000
#define NVME_CC_IOSQES      0x00060000  // I/O Submission Queue Entry Size (64 bytes = 6)
#define NVME_CC_IOCQES      0x00400000  // I/O Completion Queue Entry Size (16 bytes = 4)

//
// Controller Status Register bits
//
#define NVME_CSTS_RDY       0x00000001
#define NVME_CSTS_CFS       0x00000002
#define NVME_CSTS_SHST_MASK 0x0000000C
#define NVME_CSTS_SHST_NORMAL 0x00000000

//
// NVMe Command Opcodes
//
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_GET_LOG_PAGE 0x02
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_GET_FEATURES 0x0A
#define NVME_CMD_FLUSH          0x00
#define NVME_CMD_WRITE          0x01
#define NVME_CMD_READ           0x02

//
// NVMe Identify CNS values
//
#define NVME_CNS_NAMESPACE    0x00
#define NVME_CNS_CONTROLLER   0x01

//
// NVMe Log Page Identifiers
//
#define NVME_LOG_PAGE_ERROR_INFO        0x01
#define NVME_LOG_PAGE_SMART_HEALTH      0x02
#define NVME_LOG_PAGE_FW_SLOT_INFO      0x03

//
// ATA Commands for SMART (used in IOCTL translation)
//
#define ATA_SMART_CMD                   0xB0
#define ATA_IDENTIFY_DEVICE             0xEC
#define ATA_SMART_READ_DATA             0xD0
#define ATA_SMART_READ_THRESHOLDS       0xD1
#define ATA_SMART_ENABLE                0xD8 // This is "SMART ENABLE OPERATIONS"
#define ATA_SMART_DISABLE               0xD9
#define ATA_SMART_RETURN_STATUS         0xDA
#define ATA_SMART_AUTOSAVE              0xD2

//
// SMART signature values (cylinder registers)
//
#define SMART_CYL_LOW                   0x4F
#define SMART_CYL_HI                    0xC2

//
// NVMe Status Codes
//
#define NVME_SC_SUCCESS         0x00

//
// Queue Flags (for CDW11 in Create I/O Queue commands)
//
#define NVME_QUEUE_PHYS_CONTIG  0x0001  // Bit 0: PC (Physically Contiguous)
#define NVME_QUEUE_IRQ_ENABLED  0x0002  // Bit 1: IEN (Interrupts Enabled)

//
// Command Dword 0 fields
//
#define NVME_CMD_PRP            0x00
#define NVME_CMD_SGL            0x40

//
// Queue sizes and scatter-gather limits
//
#define NVME_SQ_ENTRY_SIZE      sizeof(NVME_COMMAND)      // Submission Queue Entry size
#define NVME_CQ_ENTRY_SIZE      sizeof(NVME_COMPLETION)   // Completion Queue Entry size
#define NVME_MAX_QUEUE_SIZE     (PAGE_SIZE/NVME_SQ_ENTRY_SIZE)  // Maximum we can fit in a page (64)
                                                                 // Actual size determined by min(NVME_MAX_QUEUE_SIZE, MQES+1)
#define SG_LIST_PAGES           10      // Number of PRP list pages (shared pool, reused across commands)
                                        // Each page holds 512 PRP entries (8 bytes each)
                                        // Max transfer per page: 512 * 4KB = 2MB
                                        // With 10 pages: up to 20MB transfers

//
// NVMe Identify Controller Structure (partial)
//
typedef struct _NVME_IDENTIFY_CONTROLLER {
    USHORT VendorId;                // Offset 0
    USHORT SubsystemVendorId;       // Offset 2
    UCHAR SerialNumber[20];         // Offset 4
    UCHAR ModelNumber[40];          // Offset 24
    UCHAR FirmwareRevision[8];      // Offset 64
    UCHAR Reserved1[444];           // Offset 72-515
    ULONG NumberOfNamespaces;       // Offset 516 (NN field)
    UCHAR Reserved2[3576];          // Offset 520-4095 (rest of 4096 byte structure)
} NVME_IDENTIFY_CONTROLLER, *PNVME_IDENTIFY_CONTROLLER;

//
// NVMe LBA Format Structure (used in Identify Namespace)
//
typedef struct _NVME_LBA_FORMAT {
    USHORT MetadataSize;        // Bits 15:0 - Metadata Size
    UCHAR LbaDataSize;          // Bits 23:16 - LBA Data Size (as a power of 2, 2^n)
    UCHAR RelativePerformance;  // Bits 31:24 - Relative Performance
} NVME_LBA_FORMAT, *PNVME_LBA_FORMAT;


//
// NVMe Identify Namespace Structure (partial)
//
typedef struct _NVME_IDENTIFY_NAMESPACE {
    ULONGLONG NamespaceSize;        // Offset 0: NSZE - Namespace Size (in logical blocks)
    ULONGLONG NamespaceCapacity;    // Offset 8: NCAP - Namespace Capacity
    ULONGLONG NamespaceUtilization; // Offset 16: NUSE - Namespace Utilization
    UCHAR NamespaceFeatures;        // Offset 24: NSFEAT
    UCHAR NumberOfLbaFormats;       // Offset 25: NLBAF
    UCHAR FormattedLbaSize;         // Offset 26: FLBAS - Formatted LBA Size
    UCHAR MetadataCapabilities;     // Offset 27: MC
    UCHAR Reserved1[100];           // Offset 28-127
    UCHAR Nguid[16];                // Offset 104-119: NGUID
    UCHAR Eui64[8];                 // Offset 120-127: EUI64
    NVME_LBA_FORMAT LbaFormats[16]; // Offset 128-191: LBAF0-LBAF15
    UCHAR Reserved2[3904];          // Offset 192-4095
} NVME_IDENTIFY_NAMESPACE, *PNVME_IDENTIFY_NAMESPACE;

//
// NVMe Command Dword 0 structure
//
// Windows 2000 ATA Pass-through structures for SMART support
// (normally defined in ntdddisk.h/winioctl.h)
//

// ATA/IDE register structure
typedef struct _IDEREGS {
    UCHAR bFeaturesReg;       // Feature register (SMART subcommand)
    UCHAR bSectorCountReg;    // Sector count
    UCHAR bSectorNumberReg;   // Sector number (LBA low)
    UCHAR bCylLowReg;         // Cylinder low (0x4F for SMART)
    UCHAR bCylHighReg;        // Cylinder high (0xC2 for SMART)
    UCHAR bDriveHeadReg;      // Drive/head
    UCHAR bCommandReg;        // Command register (0xB0 for SMART)
    UCHAR bReserved;
} IDEREGS, *PIDEREGS, *LPIDEREGS;

// Input parameters for IOCTL_SCSI_MINIPORT
typedef struct _SENDCMDINPARAMS {
    ULONG cBufferSize;        // Buffer size in bytes
    IDEREGS irDriveRegs;      // IDE register values
    UCHAR bDriveNumber;       // Physical drive number
    UCHAR bReserved[3];
    ULONG dwReserved[4];
    UCHAR bBuffer[1];         // Variable length buffer for input data
} SENDCMDINPARAMS, *PSENDCMDINPARAMS, *LPSENDCMDINPARAMS;

// Driver status
typedef struct _DRIVERSTATUS {
    UCHAR bDriverError;       // Error code from driver
    UCHAR bIDEError;          // Error register from IDE controller
    UCHAR bReserved[2];
    ULONG dwReserved[2];
} DRIVERSTATUS, *PDRIVERSTATUS, *LPDRIVERSTATUS;

// Output parameters for IOCTL_SCSI_MINIPORT
typedef struct _SENDCMDOUTPARAMS {
    ULONG cBufferSize;            // Buffer size in bytes
    DRIVERSTATUS DriverStatus;    // Driver status
    UCHAR bBuffer[1];             // Variable length buffer (512 bytes for SMART data)
} SENDCMDOUTPARAMS, *PSENDCMDOUTPARAMS, *LPSENDCMDOUTPARAMS;

// SRB_IO_CONTROL header for IOCTL_SCSI_MINIPORT
typedef struct _SRB_IO_CONTROL {
    ULONG HeaderLength;       // Length of this header (sizeof(SRB_IO_CONTROL))
    UCHAR Signature[8];       // Signature string (e.g., "SCSIDISK")
    ULONG Timeout;            // Timeout value in seconds
    ULONG ControlCode;        // Control code (miniport-specific)
    ULONG ReturnCode;         // Return code from miniport
    ULONG Length;             // Length of data buffer following this header
} SRB_IO_CONTROL, *PSRB_IO_CONTROL;

// NVMe SMART/Health Information Log (Log Page 0x02)
// Note: This structure matches the NVMe spec byte layout exactly
// Use byte arrays for unaligned fields to avoid Alpha alignment issues
#pragma pack(push, 1)
typedef struct _NVME_SMART_INFO {
    UCHAR CriticalWarning;           // Byte 0: Critical warning flags
    UCHAR Temperature[2];            // Bytes 1-2: Composite temperature (Kelvin, little-endian)
    UCHAR AvailableSpare;            // Byte 3: Available spare (%)
    UCHAR AvailableSpareThreshold;   // Byte 4: Available spare threshold (%)
    UCHAR PercentageUsed;            // Byte 5: Percentage used (%)
    UCHAR Reserved1[26];             // Bytes 6-31: Reserved
    UCHAR DataUnitsRead[16];         // Bytes 32-47: Data units read (128-bit, little-endian)
    UCHAR DataUnitsWritten[16];      // Bytes 48-63: Data units written (128-bit)
    UCHAR HostReadCommands[16];      // Bytes 64-79: Host read commands (128-bit)
    UCHAR HostWriteCommands[16];     // Bytes 80-95: Host write commands (128-bit)
    UCHAR ControllerBusyTime[16];    // Bytes 96-111: Controller busy time (128-bit)
    UCHAR PowerCycles[16];           // Bytes 112-127: Power cycles (128-bit)
    UCHAR PowerOnHours[16];          // Bytes 128-143: Power on hours (128-bit)
    UCHAR UnsafeShutdowns[16];       // Bytes 144-159: Unsafe shutdowns (128-bit)
    UCHAR MediaErrors[16];           // Bytes 160-175: Media errors (128-bit)
    UCHAR NumErrorLogEntries[16];    // Bytes 176-191: Number of error log entries (128-bit)
    UCHAR WarningTempTime[4];        // Bytes 192-195: Warning composite temp time
    UCHAR CriticalTempTime[4];       // Bytes 196-199: Critical composite temp time
    UCHAR TempSensor[16];            // Bytes 200-215: Temperature sensors 1-8 (8 x 16-bit)
    UCHAR Reserved2[296];            // Bytes 216-511: Reserved
} NVME_SMART_INFO, *PNVME_SMART_INFO;
#pragma pack(pop)

// ATA SMART Attribute (12 bytes each)
// Use byte arrays for potentially unaligned fields (Alpha compatibility)
#pragma pack(push, 1)
typedef struct _ATA_SMART_ATTRIBUTE {
    UCHAR Id;                        // Attribute ID
    UCHAR Flags[2];                  // Status flags (little-endian USHORT)
    UCHAR CurrentValue;              // Current normalized value (100 = new, 1 = worn)
    UCHAR WorstValue;                // Worst value seen
    UCHAR RawValue[6];               // Raw value (vendor specific format)
    UCHAR Reserved;
} ATA_SMART_ATTRIBUTE, *PATA_SMART_ATTRIBUTE;

// ATA SMART Data Structure (512 bytes)
typedef struct _ATA_SMART_DATA {
    UCHAR Version[2];                // Version number (little-endian USHORT)
    ATA_SMART_ATTRIBUTE Attributes[30]; // 30 attributes * 12 bytes = 360 bytes
    UCHAR OfflineDataCollectionStatus;
    UCHAR SelfTestExecutionStatus;
    UCHAR TotalTimeToCompleteOfflineDataCollection[2]; // little-endian USHORT
    UCHAR VendorSpecific1;
    UCHAR OfflineDataCollectionCapability;
    UCHAR SmartCapability[2];        // little-endian USHORT
    UCHAR ErrorLoggingCapability;
    UCHAR VendorSpecific2;
    UCHAR ShortSelfTestPollingTime;
    UCHAR ExtendedSelfTestPollingTime;
    UCHAR ConveyanceSelfTestPollingTime;
    UCHAR ExtendedSelfTestPollingTimeWord[2]; // little-endian USHORT
    UCHAR Reserved1[9];
    UCHAR VendorSpecific3[125];
    UCHAR Checksum;                  // Checksum of bytes 0-511 (sum = 0)
} ATA_SMART_DATA, *PATA_SMART_DATA;
#pragma pack(pop)

// Common ATA SMART Attribute IDs
#define ATA_SMART_ATTR_READ_ERROR_RATE          1
#define ATA_SMART_ATTR_THROUGHPUT_PERFORMANCE   2
#define ATA_SMART_ATTR_SPIN_UP_TIME             3
#define ATA_SMART_ATTR_START_STOP_COUNT         4
#define ATA_SMART_ATTR_REALLOCATED_SECTOR_COUNT 5
#define ATA_SMART_ATTR_SEEK_ERROR_RATE          7
#define ATA_SMART_ATTR_SEEK_TIME_PERFORMANCE    8
#define ATA_SMART_ATTR_POWER_ON_HOURS           9
#define ATA_SMART_ATTR_SPIN_RETRY_COUNT         10
#define ATA_SMART_ATTR_RECALIBRATION_RETRIES    11
#define ATA_SMART_ATTR_POWER_CYCLE_COUNT        12
#define ATA_SMART_ATTR_AIRFLOW_TEMPERATURE      190
#define ATA_SMART_ATTR_TEMPERATURE              194
#define ATA_SMART_ATTR_REALLOCATED_EVENT_COUNT  196
#define ATA_SMART_ATTR_CURRENT_PENDING_SECTORS  197
#define ATA_SMART_ATTR_OFFLINE_UNCORRECTABLE    198
#define ATA_SMART_ATTR_UDMA_CRC_ERROR_COUNT     199
#define ATA_SMART_ATTR_WEAR_LEVELING_COUNT      173
#define ATA_SMART_ATTR_PROGRAM_FAIL_COUNT       181
#define ATA_SMART_ATTR_ERASE_FAIL_COUNT         182
#define ATA_SMART_ATTR_REPORTED_UNCORRECTABLE   187
#define ATA_SMART_ATTR_COMMAND_TIMEOUT          188
#define ATA_SMART_ATTR_HIGH_FLY_WRITES          189
#define ATA_SMART_ATTR_TOTAL_LBA_WRITTEN        241
#define ATA_SMART_ATTR_TOTAL_LBA_READ           242

//
typedef union _NVME_CDW0 {
    struct {
        UCHAR Opcode;           // Bits 7:0 - Command Opcode
        UCHAR Flags;            // Bits 15:8 - Fused Operation (bit 0-1), Reserved (2-5), PSDT (6-7)
        USHORT CommandId;       // Bits 31:16 - Command Identifier
    } Fields;
    ULONG AsUlong;
} NVME_CDW0, *PNVME_CDW0;

//
// NVMe Submission Queue Entry
//
typedef struct _NVME_COMMAND {
    NVME_CDW0 CDW0;     // Command Dword 0 (Opcode and flags)
    ULONG NSID;         // Namespace ID
    ULONG CDW2;
    ULONG CDW3;
    ULONGLONG MPTR;     // Metadata Pointer
    ULONGLONG PRP1;     // Physical Region Page 1
    ULONGLONG PRP2;     // Physical Region Page 2
    ULONG CDW10;
    ULONG CDW11;
    ULONG CDW12;
    ULONG CDW13;
    ULONG CDW14;
    ULONG CDW15;
} NVME_COMMAND, *PNVME_COMMAND;

//
// NVMe Completion Queue Entry
//
typedef struct _NVME_COMPLETION {
    ULONG DW0;          // Command-specific
    ULONG DW1;          // Reserved
    USHORT SQHead;      // Submission Queue Head
    USHORT SQID;        // Submission Queue ID
    USHORT CID;         // Command ID
    USHORT Status;      // Status and phase
} NVME_COMPLETION, *PNVME_COMPLETION;

//
// NVMe Queue Pair
//
typedef struct _NVME_QUEUE {
    PVOID SubmissionQueue;
    PVOID CompletionQueue;
    PHYSICAL_ADDRESS SubmissionQueuePhys;
    PHYSICAL_ADDRESS CompletionQueuePhys;
    ULONG SubmissionQueueHead;   // Protected by SubmissionLock
    ULONG SubmissionQueueTail;   // Protected by SubmissionLock
    ULONG CompletionQueueHead;   // Monotonic counter, never wraps. Phase = (head >> QueueSizeBits) & 1. Protected by CompletionLock
    ULONG CompletionQueueTail;
    USHORT QueueSizeMask;        // QueueSize - 1, for index masking
    USHORT QueueId;
    USHORT QueueSize;
    UCHAR QueueSizeBits;         // log2(QueueSize), for phase calculation
    UCHAR Reserved;              // Padding for alignment
    ATOMIC SubmissionLock;       // Spinlock for NvmeSubmitCommand (0 = unlocked, 1 = locked)
    ATOMIC CompletionLock;       // Spinlock for completion processing (0 = unlocked, 1 = locked)
} NVME_QUEUE, *PNVME_QUEUE;

// Command ID encoding
// Bit 15: Set for non-tagged request
// Bit 14: Set for ORDERED flush (used with bit 15 clear)
// Bits 0-13: QueueTag (tagged) or sequence number (non-tagged)
#define CID_NON_TAGGED_FLAG 0x8000
#define CID_ORDERED_FLUSH_FLAG 0x4000
#define CID_VALUE_MASK      0x3FFF

//
// SRB Extension - per-request data stored by ScsiPort
//
typedef struct _NVME_SRB_EXTENSION {
    UCHAR PrpListPage;              // Which PRP list page is allocated (0xFF if none)
    UCHAR Reserved[3];              // Padding for alignment
} NVME_SRB_EXTENSION, *PNVME_SRB_EXTENSION;

//
// Admin Command IDs for initialization sequence
// These double as both Command IDs and state tracking
//
#define ADMIN_CID_CREATE_IO_CQ          1
#define ADMIN_CID_CREATE_IO_SQ          2
#define ADMIN_CID_IDENTIFY_CONTROLLER   3
#define ADMIN_CID_IDENTIFY_NAMESPACE    4
#define ADMIN_CID_INIT_COMPLETE         5

//
// Admin Command IDs for post-init operations (must be > ADMIN_CID_INIT_COMPLETE)
//
#define ADMIN_CID_GET_LOG_PAGE          6   // Get Log Page (untagged, only one at a time)

//
// Device extension structure - stores per-adapter data
//
typedef struct _HW_DEVICE_EXTENSION {
    ULONG AdapterIndex;
    ULONG Reserved1;                // Padding for 8-byte alignment of pointers on Alpha
    PVOID MappedAddress;
    ULONG IoPortBase;
    ULONG BusNumber;
    ULONG SlotNumber;
    USHORT VendorId;
    USHORT DeviceId;
    USHORT SubsystemVendorId;
    USHORT SubsystemId;
    UCHAR RevisionId;
    UCHAR Reserved2[3];             // Padding for alignment
    PVOID ControllerRegisters;
    ULONG ControllerRegistersLength;

    // NVMe specific fields
    ULONG Reserved3;                // Padding for 8-byte alignment
    ULONGLONG ControllerCapabilities;
    ULONG Version;
    ULONG PageSize;
    ULONG DoorbellStride;
    USHORT MaxQueueEntries;         // MQES from CAP register + 1 (actual max queue size)
    USHORT Reserved4;               // Padding for alignment

    // Admin Queue
    NVME_QUEUE AdminQueue;

    // I/O Queue (single queue for simplicity)
    NVME_QUEUE IoQueue;

    // Command tracking
    USHORT NextNonTaggedId;                     // Sequence number for non-tagged requests (bits 0-13)
    BOOLEAN NonTaggedInFlight;                  // TRUE if a non-tagged request is currently in flight
    BOOLEAN InitComplete;
    // Note: 2+1+1 = 4 bytes, next ATOMIC is naturally aligned

    // SMP synchronization for interrupt handler
    ATOMIC InterruptLock;               // 0 = unlocked, 1 = locked

    // PRP list pages for scatter-gather (shared pool, allocated after init)
    // Note: During init, UtilityBuffer points to the same memory
    PVOID PrpListPages;                     // Virtual address of PRP list pool
    PHYSICAL_ADDRESS PrpListPagesPhys;      // Physical address of PRP list pool
    USHORT PrpListPageBitmap;               // Bitmap for PRP list page allocation (10 bits used)

    // Statistics (current and maximum)
    USHORT CurrentQueueDepth;               // Current number of commands in flight
    USHORT MaxQueueDepthReached;            // Maximum number of commands in flight simultaneously
    UCHAR CurrentPrpListPagesUsed;          // Current number of PRP list pages allocated
    UCHAR MaxPrpListPagesUsed;              // Maximum number of PRP list pages allocated simultaneously

    // I/O statistics
    ULONG TotalRequests;                    // Total number of I/O requests
    ULONG TotalReads;                       // Total read operations
    ULONG TotalWrites;                      // Total write operations
    ULONGLONG TotalBytesRead;               // Total bytes read
    ULONGLONG TotalBytesWritten;            // Total bytes written
    ULONG MaxReadSize;                      // Largest read request in bytes
    ULONG MaxWriteSize;                     // Largest write request in bytes
    ULONG RejectedRequests;                 // Requests rejected due to queue full

    // Utility buffer (4KB, used during init, then aliased as PRP list pages)
    PVOID UtilityBuffer;
    PHYSICAL_ADDRESS UtilityBufferPhys;

    // Controller information
    UCHAR ControllerSerialNumber[21];     // Null-terminated
    UCHAR ControllerModelNumber[41];      // Null-terminated
    UCHAR ControllerFirmwareRevision[9];  // Null-terminated
    ULONG NumberOfNamespaces;

    // Namespace information
    ULONGLONG NamespaceSizeInBlocks;
    ULONG NamespaceBlockSize;

    // Uncached memory allocation
    PVOID UncachedExtensionBase;
    PHYSICAL_ADDRESS UncachedExtensionPhys;
    ULONG UncachedExtensionSize;
    ULONG UncachedExtensionOffset;  // Current allocation offset

} HW_DEVICE_EXTENSION, *PHW_DEVICE_EXTENSION;

//
// Forward declarations of miniport entry points
//
ULONG DriverEntry(IN PVOID DriverObject, IN PVOID Argument2);
ULONG HwFindAdapter(IN PVOID DeviceExtension, IN PVOID HwContext,
                    IN PVOID BusInformation, IN PCHAR ArgumentString,
                    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                    OUT PBOOLEAN Again);
BOOLEAN HwInitialize(IN PVOID DeviceExtension);
BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN HwInterrupt(IN PVOID DeviceExtension);
BOOLEAN HwResetBus(IN PVOID DeviceExtension, IN ULONG PathId);
SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(IN PVOID DeviceExtension,
                                              IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
                                              IN PVOID Parameters);

//
// Helper functions
//
BOOLEAN IsNvmeDevice(UCHAR BaseClass, UCHAR SubClass, UCHAR ProgIf);
USHORT ReadPciConfigWord(IN PVOID DeviceExtension, IN ULONG Offset);
ULONG ReadPciConfigDword(IN PVOID DeviceExtension, IN ULONG Offset);
VOID WritePciConfigWord(IN PVOID DeviceExtension, IN ULONG Offset, IN USHORT Value);
VOID WritePciConfigDword(IN PVOID DeviceExtension, IN ULONG Offset, IN ULONG Value);

//
// NVMe helper functions
//
ULONG NvmeReadReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
VOID NvmeWriteReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value);
ULONGLONG NvmeReadReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
VOID NvmeWriteReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONGLONG Value);
BOOLEAN NvmeWaitForReady(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN WaitForReady);
BOOLEAN NvmeSubmitCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_QUEUE Queue, IN PNVME_COMMAND Cmd);
BOOLEAN NvmeProcessAdminCompletion(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeProcessIoCompletion(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeRingDoorbell(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT QueueId, IN BOOLEAN IsSubmission, IN USHORT Value);
BOOLEAN NvmeCreateIoCQ(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeCreateIoSQ(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeBuildReadWriteCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN PNVME_COMMAND Cmd, IN USHORT CommandId);
USHORT NvmeBuildCommandId(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
USHORT NvmeBuildFlushCommandId(IN PSCSI_REQUEST_BLOCK Srb);
PSCSI_REQUEST_BLOCK NvmeGetSrbFromCommandId(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT CommandId);
BOOLEAN NvmeIdentifyController(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeIdentifyNamespace(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeGetLogPage(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR LogPageId);

//
// SMART/IOCTL functions
//
BOOLEAN HandleSmartIoctl(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
VOID NvmeSmartToAtaSmart(IN PNVME_SMART_INFO NvmeSmart, OUT PATA_SMART_DATA AtaSmart);
VOID ScsiHandleInquiry(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
VOID ScsiHandleReadCapacity(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
VOID ScsiHandleReadWrite(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
VOID ScsiHandleFlush(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);

//
// PRP list page allocator
//
UCHAR AllocatePrpListPage(IN PHW_DEVICE_EXTENSION DevExt);
VOID FreePrpListPage(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR PageIndex);
PVOID GetPrpListPageVirtual(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR PageIndex);
PHYSICAL_ADDRESS GetPrpListPagePhysical(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR PageIndex);

//
// Uncached memory allocator
//
BOOLEAN AllocateUncachedMemory(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Size, IN ULONG Alignment,
                               OUT PVOID* VirtualAddress, OUT PHYSICAL_ADDRESS* PhysicalAddress);

#endif // _NVME2K_H_
