//
// utils.c - Utility functions for NVMe2K driver
//

#include "utils.h"

//
// RtlCompareMemory - Compare two memory blocks, return number of matching bytes
//
ULONG RtlCompareMemory(IN CONST VOID *Source1, IN CONST VOID *Source2, IN ULONG Length)
{
    PUCHAR s1 = (PUCHAR)Source1;
    PUCHAR s2 = (PUCHAR)Source2;
    ULONG i;

    for (i = 0; i < Length; i++) {
        if (s1[i] != s2[i]) {
            return i;
        }
    }
    return Length;
}

//
// NvmeSmartToAtaSmart - Convert NVMe SMART/Health log to ATA SMART format
// Alpha-safe: All field accesses are byte-aligned
//
VOID NvmeSmartToAtaSmart(IN PNVME_SMART_INFO NvmeSmart, OUT PATA_SMART_DATA AtaSmart)
{
    UCHAR attrIndex;
    UCHAR checksum;
    ULONG i;
    USHORT tempCelsius;
    USHORT tempValue;
    ULONG zero = 0;

    attrIndex = 0;

    /* Zero out the entire structure */
    RtlZeroMemory(AtaSmart, sizeof(ATA_SMART_DATA));

    /* Set version */
    WRITE_USHORT(AtaSmart->Version, 0x0010);  /* SMART version 1.0 */

    /* Helper macro to add an attribute - Alpha-safe byte access */
#define ADD_ATTRIBUTE(id, current, worst, raw) \
    if (attrIndex < 30) { \
        AtaSmart->Attributes[attrIndex].Id = (id); \
        WRITE_USHORT(AtaSmart->Attributes[attrIndex].Flags, 0x0003); \
        AtaSmart->Attributes[attrIndex].CurrentValue = (current); \
        AtaSmart->Attributes[attrIndex].WorstValue = (worst); \
        WRITE_ULONG(AtaSmart->Attributes[attrIndex].RawValue, (raw)); \
        AtaSmart->Attributes[attrIndex].RawValue[4] = 0; \
        AtaSmart->Attributes[attrIndex].RawValue[5] = 0; \
        attrIndex++; \
    }

    /* 1. Power-On Hours (Attribute 9) */
    /* NVMe PowerOnHours is in minutes, ATA expects hours */
    {
        ULONGLONG powerOnMinutes = READ_ULONGLONG(NvmeSmart->PowerOnHours);
        ULONG powerOnHours;
        if (powerOnMinutes > 0) {
            powerOnHours = (ULONG)(powerOnMinutes / 60);
            ADD_ATTRIBUTE(ATA_SMART_ATTR_POWER_ON_HOURS, 100, 100, powerOnHours);
        }
    }

    /* 2. Power Cycle Count (Attribute 12) */
    {
        ULONGLONG powerCycles64 = READ_ULONGLONG(NvmeSmart->PowerCycles);
        if (powerCycles64 > 0) {
            ULONG powerCycles = (ULONG)powerCycles64;
            ADD_ATTRIBUTE(ATA_SMART_ATTR_POWER_CYCLE_COUNT, 100, 100, powerCycles);
        }
    }

    /* 3. Temperature (Attribute 194) */
    /* NVMe temperature is in Kelvin, convert to Celsius */
    tempCelsius = READ_USHORT(NvmeSmart->Temperature);
    if (tempCelsius > 0) {
        ULONG temp;
        UCHAR tempVal;
        tempCelsius = (tempCelsius > 273) ? (tempCelsius - 273) : 0;
        temp = (ULONG)tempCelsius;
        /* Current value: higher temp = lower value (inverse relationship) */
        tempVal = (tempCelsius < 100) ? (100 - (UCHAR)tempCelsius) : 0;
        ADD_ATTRIBUTE(ATA_SMART_ATTR_TEMPERATURE, tempVal, tempVal, temp);
    }

    // 4. Wear Leveling / Percentage Used (Attribute 173)
    // NVMe PercentageUsed: 0-100%, higher is more worn
    if (NvmeSmart->PercentageUsed > 0) {
        UCHAR percentUsed = NvmeSmart->PercentageUsed;
        UCHAR wearValue = (percentUsed <= 100) ? (100 - percentUsed) : 0;
        ULONG percentRaw = percentUsed;
        ADD_ATTRIBUTE(ATA_SMART_ATTR_WEAR_LEVELING_COUNT, wearValue, wearValue, percentRaw);
    }

    // 5. Available Spare (not a standard ATA attribute, use vendor-specific 170)
    if (NvmeSmart->AvailableSpare > 0) {
        ULONG spare = NvmeSmart->AvailableSpare;
        UCHAR spareValue = NvmeSmart->AvailableSpare;
        ADD_ATTRIBUTE(170, spareValue, spareValue, spare);
    }

    // 6. Media/Data Integrity Errors (Attribute 199 - UDMA CRC Error as proxy)
    if (NvmeSmart->MediaErrors[0] > 0 || NvmeSmart->MediaErrors[1] > 0) {
        ULONG mediaErrors = (ULONG)NvmeSmart->MediaErrors[0];
        UCHAR errorValue = (mediaErrors == 0) ? 100 : ((mediaErrors < 100) ? (100 - (UCHAR)mediaErrors) : 0);
        ADD_ATTRIBUTE(ATA_SMART_ATTR_UDMA_CRC_ERROR_COUNT, errorValue, errorValue, mediaErrors);
    }

    // 7. Unsafe Shutdowns (Attribute 192 - Power-off Retract Count as proxy)
    if (NvmeSmart->UnsafeShutdowns[0] > 0 || NvmeSmart->UnsafeShutdowns[1] > 0) {
        ULONG unsafeShutdowns = (ULONG)NvmeSmart->UnsafeShutdowns[0];
        UCHAR shutdownValue = (unsafeShutdowns == 0) ? 100 : ((unsafeShutdowns < 100) ? (100 - (UCHAR)unsafeShutdowns) : 0);
        ADD_ATTRIBUTE(192, shutdownValue, shutdownValue, unsafeShutdowns);
    }

    // 8. Total LBAs Written (Attribute 241) - if data units written is non-zero
    if (NvmeSmart->DataUnitsWritten[0] > 0 || NvmeSmart->DataUnitsWritten[1] > 0) {
        ULONG lbasWritten = (ULONG)NvmeSmart->DataUnitsWritten[0];
        ADD_ATTRIBUTE(ATA_SMART_ATTR_TOTAL_LBA_WRITTEN, 100, 100, lbasWritten);
    }

    // 9. Total LBAs Read (Attribute 242)
    if (NvmeSmart->DataUnitsRead[0] > 0 || NvmeSmart->DataUnitsRead[1] > 0) {
        ULONG lbasRead = (ULONG)NvmeSmart->DataUnitsRead[0];
        ADD_ATTRIBUTE(ATA_SMART_ATTR_TOTAL_LBA_READ, 100, 100, lbasRead);
    }

    // 10. Add some zero-filled mechanical drive attributes (not applicable to NVMe)
    // Seek Error Rate (7) - N/A for SSDs
    ADD_ATTRIBUTE(ATA_SMART_ATTR_SEEK_ERROR_RATE, 100, 100, zero);

    // Spin Up Time (3) - N/A for SSDs
    ADD_ATTRIBUTE(ATA_SMART_ATTR_SPIN_UP_TIME, 100, 100, zero);

    // Start/Stop Count (4) - Use power cycles as approximation
    {
        ULONG startStops;
        startStops = (ULONG)NvmeSmart->PowerCycles[0];
        ADD_ATTRIBUTE(ATA_SMART_ATTR_START_STOP_COUNT, 100, 100, startStops);
    }

#undef ADD_ATTRIBUTE

    // Set capabilities and status
    AtaSmart->OfflineDataCollectionStatus = 0x00;  // Never started
    AtaSmart->SelfTestExecutionStatus = 0x00;      // No self-test in progress
    WRITE_USHORT(AtaSmart->TotalTimeToCompleteOfflineDataCollection, 0);
    AtaSmart->OfflineDataCollectionCapability = 0x01;  // Offline supported
    WRITE_USHORT(AtaSmart->SmartCapability, 0x0003);  // Attribute autosave, SMART enabled
    AtaSmart->ErrorLoggingCapability = 0x01;  // Error logging supported

    // Calculate checksum (sum of all 512 bytes should be 0)
    checksum = 0;
    for (i = 0; i < 511; i++) {
        checksum += ((PUCHAR)AtaSmart)[i];
    }
    AtaSmart->Checksum = (UCHAR)(0x100 - checksum);
}