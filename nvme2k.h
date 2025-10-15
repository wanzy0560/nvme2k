//
// NVMe Driver for Windows 2000 - Header File
// Contains all defines, constants, and type definitions
//

#ifndef _NVME2K_H_
#define _NVME2K_H_

#include <miniport.h>
#include <scsi.h>
#include <devioctl.h>
#include <ntddscsi.h>
#include "atomic.h"

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
#define NVME_CC_SHN_NORMAL  0x00004000  // Normal shutdown notification (bits 15:14 = 01b)
#define NVME_CC_SHN_ABRUPT  0x00008000  // Abrupt shutdown notification (bits 15:14 = 10b)
#define NVME_CC_SHN_MASK    0x0000C000  // Shutdown notification mask
#define NVME_CC_IOSQES      0x00060000  // I/O Submission Queue Entry Size (64 bytes = 6)
#define NVME_CC_IOCQES      0x00400000  // I/O Completion Queue Entry Size (16 bytes = 4)

//
// Controller Status Register bits
//
#define NVME_CSTS_RDY       0x00000001
#define NVME_CSTS_CFS       0x00000002  // Controller Fatal Status
#define NVME_CSTS_SHST_MASK 0x0000000C  // Shutdown Status mask (bits 3:2)
#define NVME_CSTS_SHST_NORMAL 0x00000000  // Normal operation (no shutdown)
#define NVME_CSTS_SHST_OCCURRING 0x00000004  // Shutdown processing occurring (bits 3:2 = 01b)
#define NVME_CSTS_SHST_COMPLETE 0x00000008  // Shutdown processing complete (bits 3:2 = 10b)

//
// NVMe Command Opcodes
//
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_GET_LOG_PAGE 0x02
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
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
// SCSI SAT ATA PASS-THROUGH Commands
//
#define SCSIOP_ATA_PASSTHROUGH16        0x85
#define SCSIOP_ATA_PASSTHROUGH12        0xA1

//
// SCSI Log Sense Page Codes (commonly used)
//
#define SCSI_LOG_PAGE_SUPPORTED_PAGES   0x00
#define SCSI_LOG_PAGE_WRITE_ERROR       0x02
#define SCSI_LOG_PAGE_READ_ERROR        0x03
#define SCSI_LOG_PAGE_VERIFY_ERROR      0x05
#define SCSI_LOG_PAGE_TEMPERATURE       0x0D
#define SCSI_LOG_PAGE_START_STOP        0x0E
#define SCSI_LOG_PAGE_SELF_TEST         0x10
#define SCSI_LOG_PAGE_INFORMATIONAL     0x2F

//
// READ DEFECT DATA (10) CDB structure
// Per SBC-3 section 5.7
//
typedef struct _CDB_READ_DEFECT_DATA {
    UCHAR OperationCode;        // Byte 0: 0x37
    UCHAR Reserved1;            // Byte 1: Reserved (bits 7-5), LUN (bits 2-0)
    UCHAR FormatByte;           // Byte 2: bit 4=Req_plist, bit 3=Req_glist, bits 2-0=format
    UCHAR Reserved3[4];         // Bytes 3-6: Reserved
    UCHAR AllocationLength[2];  // Bytes 7-8: Allocation length (big-endian)
    UCHAR Control;              // Byte 9: Control byte
                                //   Bit 7-6: Vendor specific
                                //   Bit 5-4: Reserved
                                //   Bit 3: NACA (Normal Auto Contingent Allegiance)
                                //   Bit 2: Reserved
                                //   Bit 1-0: Reserved/Link (obsolete)
} CDB_READ_DEFECT_DATA, *PCDB_READ_DEFECT_DATA;

//
// ATA Commands for SMART (used in IOCTL translation)
//
#define ATA_SMART_CMD                   0xB0
// smart actions for ATA_SMART_CMD
#define ATA_SMART_READ_DATA             0xD0
#define ATA_SMART_READ_THRESHOLDS       0xD1
#define ATA_SMART_ENABLE                0xD8 // This is "SMART ENABLE OPERATIONS"
#define ATA_SMART_DISABLE               0xD9
#define ATA_SMART_RETURN_STATUS         0xDA
#define ATA_SMART_AUTOSAVE              0xD2
#define ATA_SMART_READ_LOG              0xD5
#define ATA_SMART_WRITE_LOG             0xD6
#define ATA_SMART_ENABLE_OPERATIONS     0x4F
// other ATA commands
#define ATA_IDENTIFY_DEVICE             0xEC
#define ATA_SMART_READ_LOG_DMA_EXT      0x57 // SMART log, bit 0
#define ATA_SMART_READ_LOG_EXT          0x58
#define ATA_SMART_WRITE_LOG_EXT         0x5B

// SCSI ops not defined in win2k scsi.h
#define SCSIOP_RESERVE6                 0x16
#define SCSIOP_RELEASE6                 0x17
#define SCSIOP_RESERVE10                0x56
#define SCSIOP_RELEASE10                0x57
#ifndef SCSIOP_READ_DEFECT_DATA10
#define SCSIOP_READ_DEFECT_DATA10       0x37
#endif

//
// SMART signature values (cylinder registers)
//
#define SMART_CYL_LOW                   0x4F
#define SMART_CYL_HI                    0xC2

//
// NVMe Status Codes (Status Code field, bits 7:1 of Status Word DW3[15:0])
// Per NVMe 1.0e specification, Figure 38
//

// Generic Command Status (Status Code Type = 0x0)
#define NVME_SC_SUCCESS                     0x00  // Successful Completion
#define NVME_SC_INVALID_OPCODE              0x01  // Invalid Command Opcode
#define NVME_SC_INVALID_FIELD               0x02  // Invalid Field in Command
#define NVME_SC_CMDID_CONFLICT              0x03  // Command ID Conflict
#define NVME_SC_DATA_XFER_ERROR             0x04  // Data Transfer Error
#define NVME_SC_POWER_LOSS                  0x05  // Commands Aborted due to Power Loss Notification
#define NVME_SC_INTERNAL                    0x06  // Internal Device Error
#define NVME_SC_ABORT_REQ                   0x07  // Command Abort Requested
#define NVME_SC_ABORT_QUEUE                 0x08  // Command Aborted due to SQ Deletion
#define NVME_SC_FUSED_FAIL                  0x09  // Command Aborted due to Failed Fused Command
#define NVME_SC_FUSED_MISSING               0x0A  // Command Aborted due to Missing Fused Command
#define NVME_SC_INVALID_NS                  0x0B  // Invalid Namespace or Format
#define NVME_SC_CMD_SEQ_ERROR               0x0C  // Command Sequence Error
#define NVME_SC_INVALID_SGL_SEG_DESC        0x0D  // Invalid SGL Segment Descriptor (NVMe 1.1+)
#define NVME_SC_INVALID_NUM_SGL_DESC        0x0E  // Invalid Number of SGL Descriptors (NVMe 1.1+)
#define NVME_SC_DATA_SGL_LEN_INVALID        0x0F  // Data SGL Length Invalid (NVMe 1.1+)
#define NVME_SC_METADATA_SGL_LEN_INVALID    0x10  // Metadata SGL Length Invalid (NVMe 1.1+)
#define NVME_SC_SGL_DESC_TYPE_INVALID       0x11  // SGL Descriptor Type Invalid (NVMe 1.1+)

// Command Specific Status (Status Code Type = 0x0)
#define NVME_SC_LBA_RANGE                   0x80  // LBA Out of Range
#define NVME_SC_CAP_EXCEEDED                0x81  // Capacity Exceeded
#define NVME_SC_NS_NOT_READY                0x82  // Namespace Not Ready
#define NVME_SC_RESERVATION_CONFLICT        0x83  // Reservation Conflict (NVMe 1.1+)

// Media Errors (Status Code Type = 0x2)
#define NVME_SC_WRITE_FAULT                 0x80  // Write Fault
#define NVME_SC_READ_ERROR                  0x81  // Unrecovered Read Error
#define NVME_SC_GUARD_CHECK                 0x82  // End-to-end Guard Check Error
#define NVME_SC_APPTAG_CHECK                0x83  // End-to-end Application Tag Check Error
#define NVME_SC_REFTAG_CHECK                0x84  // End-to-end Reference Tag Check Error
#define NVME_SC_COMPARE_FAILED              0x85  // Compare Failure
#define NVME_SC_ACCESS_DENIED               0x86  // Access Denied

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
#if 0 
// introduced after XP   
    ULONG dwReserved[4];
#endif    
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

//
// ATA IDENTIFY DEVICE Data Structure (512 bytes)
// This is the response to the ATA IDENTIFY DEVICE command (0xEC)
// We'll emulate an LBA-capable IDE drive
//
typedef struct _ATA_IDENTIFY_DEVICE_STRUCT {
    UCHAR GeneralConfiguration[2];       // Word 0: General configuration (little-endian)
    UCHAR NumCylinders[2];               // Word 1: Number of logical cylinders
    UCHAR Reserved1[2];                  // Word 2: Specific configuration
    UCHAR NumHeads[2];                   // Word 3: Number of logical heads
    UCHAR Reserved2[4];                  // Word 4-5: Retired
    UCHAR NumSectorsPerTrack[2];         // Word 6: Number of logical sectors per track
    UCHAR Reserved3[6];                  // Word 7-9: Reserved
    UCHAR SerialNumber[20];              // Word 10-19: Serial number (ASCII, padded with spaces)
    UCHAR Reserved4[6];                  // Word 20-22: Retired
    UCHAR FirmwareRevision[8];           // Word 23-26: Firmware revision (ASCII, padded with spaces)
    UCHAR ModelNumber[40];               // Word 27-46: Model number (ASCII, padded with spaces)
    UCHAR MaxMultipleSectors[2];         // Word 47: Maximum sectors per interrupt
    UCHAR Reserved5[2];                  // Word 48: Trusted Computing
    UCHAR Capabilities[4];               // Word 49-50: Capabilities (LBA, DMA, etc.)
    UCHAR Reserved6[4];                  // Word 51-52: Obsolete
    UCHAR ValidFields[2];                // Word 53: Valid translation fields
    UCHAR CurrentCylinders[2];           // Word 54: Current logical cylinders
    UCHAR CurrentHeads[2];               // Word 55: Current logical heads
    UCHAR CurrentSectorsPerTrack[2];     // Word 56: Current logical sectors per track
    UCHAR CurrentCapacityLow[2];         // Word 57: Current capacity low word
    UCHAR CurrentCapacityHigh[2];        // Word 58: Current capacity high word
    UCHAR MultipleSectorSetting[2];      // Word 59: Multiple sector setting
    UCHAR TotalAddressableSectors[4];    // Word 60-61: Total user addressable sectors (LBA-28)
    UCHAR Reserved7[2];                  // Word 62: Obsolete
    UCHAR MultiwordDmaMode[2];           // Word 63: Multiword DMA modes
    UCHAR PioModesSupported[2];          // Word 64: PIO modes supported
    UCHAR MinMdmaCycleTime[2];           // Word 65: Minimum MDMA transfer cycle time
    UCHAR RecommendedMdmaCycleTime[2];   // Word 66: Recommended MDMA transfer cycle time
    UCHAR MinPioCycleTime[2];            // Word 67: Minimum PIO transfer cycle time
    UCHAR MinPioCycleTimeIordy[2];       // Word 68: Minimum PIO cycle time with IORDY
    UCHAR Reserved8[12];                 // Word 69-74: Reserved
    UCHAR QueueDepth[2];                 // Word 75: Queue depth
    UCHAR Reserved9[8];                  // Word 76-79: Reserved for SATA
    UCHAR MajorVersion[2];               // Word 80: Major version number
    UCHAR MinorVersion[2];               // Word 81: Minor version number
    UCHAR CommandSetSupported1[2];       // Word 82: Command sets supported
    UCHAR CommandSetSupported2[2];       // Word 83: Command sets supported
    UCHAR CommandSetSupportedExt[2];     // Word 84: Command sets supported extended
    UCHAR CommandSetEnabled1[2];         // Word 85: Command sets enabled
    UCHAR CommandSetEnabled2[2];         // Word 86: Command sets enabled
    UCHAR CommandSetDefault[2];          // Word 87: Command set default
    UCHAR UltraDmaMode[2];               // Word 88: Ultra DMA modes
    UCHAR TimeForSecurityErase[2];       // Word 89: Time required for security erase
    UCHAR TimeForEnhancedErase[2];       // Word 90: Time required for enhanced erase
    UCHAR CurrentPowerManagement[2];     // Word 91: Current advanced power management
    UCHAR MasterPasswordRevision[2];     // Word 92: Master password revision
    UCHAR HardwareResetResult[2];        // Word 93: Hardware reset result
    UCHAR Reserved10[12];                // Word 94-99: Reserved
    UCHAR TotalAddressableSectors48[8];  // Word 100-103: Total addressable sectors (LBA-48)
    UCHAR Reserved11[44];                // Word 104-125: Reserved
    UCHAR RemovableMediaStatus[2];       // Word 126: Removable media status notification
    UCHAR SecurityStatus[2];             // Word 127: Security status
    UCHAR VendorSpecific[62];            // Word 128-158: Vendor specific
    UCHAR Reserved12[194];               // Word 159-255: Reserved
} ATA_IDENTIFY_DEVICE_STRUCT, *PATA_IDENTIFY_DEVICE_STRUCT;

#pragma pack(pop)

//
// SCSI Log Page Structures
//
#pragma pack(push, 1)

// SCSI Log Page Header (4 bytes)
typedef struct _SCSI_LOG_PAGE_HEADER {
    UCHAR PageCode;              // Bits 5:0 = Page Code, Bits 7:6 = Page Control
    UCHAR Reserved;
    UCHAR PageLength[2];         // Big-endian: Total length of parameters (not including header)
} SCSI_LOG_PAGE_HEADER, *PSCSI_LOG_PAGE_HEADER;

// SCSI Log Parameter Header (4 bytes)
typedef struct _SCSI_LOG_PARAMETER {
    UCHAR ParameterCode[2];      // Big-endian: Parameter code
    UCHAR ControlByte;           // Bits: DU, DS, TSD, ETC, TMC, LBIN, LP
    UCHAR ParameterLength;       // Length of parameter value (bytes following this header)
} SCSI_LOG_PARAMETER, *PSCSI_LOG_PARAMETER;

//
// SAT (SCSI/ATA Translation) ATA PASS-THROUGH CDB structures
//
typedef struct _SAT_PASSTHROUGH_16 {
    UCHAR OperationCode;     // 0x85
    UCHAR Protocol : 4;      // Transfer protocol (4=PIO Data-In for SMART)
    UCHAR Multiple : 3;      // Multiple count
    UCHAR Extend : 1;        // 1=48-bit command
    UCHAR Offline : 2;       // Offline control
    UCHAR CkCond : 1;        // Check condition
    UCHAR TType : 1;         // Transfer type
    UCHAR TDir : 1;          // Transfer direction (1=device to host)
    UCHAR ByteBlock : 1;     // Byte/block
    UCHAR TLength : 2;       // Transfer length
    UCHAR Features15_8;      // Features register (15:8)
    UCHAR Features7_0;       // Features register (7:0)
    UCHAR SectorCount15_8;   // Sector count (15:8)
    UCHAR SectorCount7_0;    // Sector count (7:0)
    UCHAR LbaLow15_8;        // LBA Low (15:8)
    UCHAR LbaLow7_0;         // LBA Low (7:0)
    UCHAR LbaMid15_8;        // LBA Mid (15:8)
    UCHAR LbaMid7_0;         // LBA Mid (7:0)
    UCHAR LbaHigh15_8;       // LBA High (15:8)
    UCHAR LbaHigh7_0;        // LBA High (7:0)
    UCHAR Device;            // Device register
    UCHAR Command;           // ATA Command
    UCHAR Control;           // Control
} SAT_PASSTHROUGH_16, *PSAT_PASSTHROUGH_16;

typedef struct _SAT_PASSTHROUGH_12 {
    UCHAR OperationCode;     // 0xA1
    UCHAR Protocol : 4;      // Transfer protocol (4=PIO Data-In for SMART)
    UCHAR Multiple : 3;      // Multiple count
    UCHAR Extend : 1;        // 1=48-bit command
    UCHAR Offline : 2;       // Offline control
    UCHAR CkCond : 1;        // Check condition
    UCHAR TType : 1;         // Transfer type
    UCHAR TDir : 1;          // Transfer direction (1=device to host)
    UCHAR ByteBlock : 1;     // Byte/block
    UCHAR TLength : 2;       // Transfer length
    UCHAR Features;          // Features register (7:0)
    UCHAR SectorCount;       // Sector count (7:0)
    UCHAR LbaLow;            // LBA Low (7:0)
    UCHAR LbaMid;            // LBA Mid (7:0)
    UCHAR LbaHigh;           // LBA High (7:0)
    UCHAR Device;            // Device register
    UCHAR Command;           // ATA Command
    UCHAR Reserved;          // Reserved
    UCHAR Control;           // Control
} SAT_PASSTHROUGH_12, *PSAT_PASSTHROUGH_12;

// SAT Protocol values
#define SAT_PROTOCOL_HARD_RESET         0
#define SAT_PROTOCOL_SRST               1
#define SAT_PROTOCOL_NON_DATA           3
#define SAT_PROTOCOL_PIO_DATA_IN        4
#define SAT_PROTOCOL_PIO_DATA_OUT       5
#define SAT_PROTOCOL_DMA                6
#define SAT_PROTOCOL_DMA_QUEUED         7
#define SAT_PROTOCOL_DEVICE_DIAGNOSTIC  8
#define SAT_PROTOCOL_DEVICE_RESET       9
#define SAT_PROTOCOL_UDMA_DATA_IN       10
#define SAT_PROTOCOL_UDMA_DATA_OUT      11
#define SAT_PROTOCOL_FPDMA              12
#define SAT_PROTOCOL_RETURN_RESPONSE    15

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
// SG_LIST_PAGES IDs reserved for page index

//
// Admin Command IDs for shutdown sequence (special, non-colliding values)
//
#define ADMIN_CID_SHUTDOWN_DELETE_SQ    0xFFFE
#define ADMIN_CID_SHUTDOWN_DELETE_CQ    0xFFFD

//
// Device extension structure - stores per-adapter data
//
typedef struct _HW_DEVICE_EXTENSION {
    ULONG AdapterIndex;                             // Offset 0x00 (0)
    PVOID MappedAddress;                            // Offset 0x04 (4)
    ULONG IoPortBase;                               // Offset 0x08 (8)
    ULONG BusNumber;                                // Offset 0x0C (12)
    ULONG SlotNumber;                               // Offset 0x10 (16)
    USHORT VendorId;                                // Offset 0x14 (20)
    USHORT DeviceId;                                // Offset 0x16 (22)
    USHORT SubsystemVendorId;                       // Offset 0x18 (24)
    USHORT SubsystemId;                             // Offset 0x1A (26)
    UCHAR RevisionId;                               // Offset 0x1C (28)
    UCHAR Reserved2[3];                             // Offset 0x1D (29)
    PVOID ControllerRegisters;                      // Offset 0x20 (32)
    ULONG ControllerRegistersLength;                // Offset 0x24 (36)

    // NVMe specific fields
    ULONGLONG ControllerCapabilities;               // Offset 0x28 (40) [8-byte aligned]
    ULONG Version;                                  // Offset 0x30 (48)
    ULONG PageSize;                                 // Offset 0x34 (52)
    ULONG DoorbellStride;                           // Offset 0x38 (56)
    USHORT MaxQueueEntries;                         // Offset 0x3C (60)
    USHORT Reserved3;                               // Offset 0x3E (62)

    // Admin Queue
    NVME_QUEUE AdminQueue;                          // Offset 0x40 (64) - 56 bytes [8-byte aligned]

    // I/O Queue (single queue for simplicity)
    NVME_QUEUE IoQueue;                             // Offset 0x78 (120) - 56 bytes [8-byte aligned]

    // Command tracking
    PSCSI_REQUEST_BLOCK NonTaggedSrbFallback;       // Offset 0xB0 (176)
    USHORT NextNonTaggedId;                         // Offset 0xB4 (180)
    BOOLEAN NonTaggedInFlight;                      // Offset 0xB6 (182)
    BOOLEAN InitComplete;                           // Offset 0xB7 (183)

    // SMP synchronization for interrupt handler
    ATOMIC InterruptLock;                           // Offset 0xB8 (184)
    ULONG Reserved4;                                // Offset 0xBC (188)

    // PRP list pages for scatter-gather (shared pool, allocated after init)
    // Note: During init, UtilityBuffer points to the same memory
    PHYSICAL_ADDRESS PrpListPagesPhys;              // Offset 0xC0 (192) [8-byte aligned]
    PVOID PrpListPages;                             // Offset 0xC8 (200)
    ULONG PrpListPageBitmap;                        // Offset 0xCC (204)

    // Statistics (current and maximum)
    ULONG CurrentQueueDepth;                        // Offset 0xD0 (208)
    ULONG MaxQueueDepthReached;                     // Offset 0xD4 (212)
    ULONG CurrentPrpListPagesUsed;                  // Offset 0xD8 (216)
    ULONG MaxPrpListPagesUsed;                      // Offset 0xDC (220)

    // I/O statistics
    ULONGLONG TotalBytesRead;                       // Offset 0xE0 (224) [8-byte aligned]
    ULONGLONG TotalBytesWritten;                    // Offset 0xE8 (232) [8-byte aligned]
    ULONG TotalRequests;                            // Offset 0xF0 (240)
    ULONG TotalReads;                               // Offset 0xF4 (244)
    ULONG TotalWrites;                              // Offset 0xF8 (248)
    ULONG MaxReadSize;                              // Offset 0xFC (252)
    ULONG MaxWriteSize;                             // Offset 0x100 (256)
    ULONG RejectedRequests;                         // Offset 0x104 (260)

    // Utility buffer (4KB, used during init, then aliased as PRP list pages)
    PHYSICAL_ADDRESS UtilityBufferPhys;             // Offset 0x108 (264) [8-byte aligned]
    PVOID UtilityBuffer;                            // Offset 0x110 (272)

    // Controller information
    ULONG NumberOfNamespaces;                       // Offset 0x114 (276)
    UCHAR ControllerSerialNumber[21];               // Offset 0x118 (280)
    UCHAR ControllerModelNumber[41];                // Offset 0x12D (301)
    UCHAR ControllerFirmwareRevision[9];            // Offset 0x156 (342)
    BOOLEAN SMARTEnabled;                           // Offset 0x15F (351)

    // Namespace information
    ULONGLONG NamespaceSizeInBlocks;                // Offset 0x160 (352) [8-byte aligned]
    ULONG NamespaceBlockSize;                       // Offset 0x168 (360)
    ULONG UncachedExtensionOffset;                  // Offset 0x16C (364)

    // Uncached memory allocation
    PHYSICAL_ADDRESS UncachedExtensionPhys;         // Offset 0x170 (368) [8-byte aligned]
    PVOID UncachedExtensionBase;                    // Offset 0x178 (376)
    ULONG UncachedExtensionSize;                    // Offset 0x17C (380)

} HW_DEVICE_EXTENSION, *PHW_DEVICE_EXTENSION;       // Total size: 0x180 (384) bytes

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
BOOLEAN NvmeSubmitIoCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd);
BOOLEAN NvmeSubmitAdminCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd);
BOOLEAN NvmeProcessAdminCompletion(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeShutdownController(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeProcessGetLogPageCompletion(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT status, USHORT commandId);
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
VOID NvmeToAtaIdentify(IN PHW_DEVICE_EXTENSION DevExt, OUT PATA_IDENTIFY_DEVICE_STRUCT AtaIdentify);
BOOLEAN NvmeLogPageToScsiLogPage(IN PNVME_SMART_INFO NvmeSmart, IN UCHAR ScsiPageCode, OUT PVOID ScsiLogBuffer, IN ULONG BufferSize, OUT PULONG BytesWritten);

// SCSI helper functions
BOOLEAN ScsiParseSatCommand(IN PSCSI_REQUEST_BLOCK Srb, OUT PUCHAR AtaCommand, OUT PUCHAR AtaFeatures, OUT PUCHAR AtaCylLow, OUT PUCHAR AtaCylHigh);
UCHAR ScsiGetLogPageCodeFromSrb(IN PSCSI_REQUEST_BLOCK Srb);

// helpers for completing SRBs
BOOLEAN ScsiSuccess(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiBusy(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiError(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR SrbStatus);
BOOLEAN ScsiPending(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);

//
// SMART/IOCTL functions
//
BOOLEAN HandleIO_SCSIDISK(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleInquiry(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadCapacity(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadWrite(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleFlush(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleLogSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleSatPassthrough(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleModeSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadDefectData10(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);

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
