/** @file
  CpuTopologySync.

  Copyright (c) 2021, vit9696. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#define ACIDANTHERA_PRIVATE
#define MSR_CORE_THREAD_COUNT 0x35

#include "CpuTopologySync.h"
#include <i386/proc_reg.h>
#include <i386/machine_routines.h>
#include <IOKit/IOLib.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_version.hpp>

OSDefineMetaClassAndStructors(CpuTopologySync, IOService)

/**
  macOS topology variables for Alder Lake i5 6P+4E (10C16T).

  cpuid_cores_per_package   = 64     // cpuid 4 eax[31:26] + 1
  cache_sharing             = 128    // cpuid 4 eax[25:14] + 1
  cpuid_logical_per_package = 128    // cpuid 1 ebx[23:16]
  core_count                = 10     // MSR 35h
  thread_count              = 16     // MSR 35h ~ somewhat wrong

  nCoresSharingLLC = [cache_sharing / (thread_count / core_count)] -> core_count = 10
  nLCPUsSharingLLC = cache_sharing -> thread_count = 16 // wrong
  maxSharingLLC    = cache_sharing = 128

  nLThreadsPerCore = thread_count / core_count = 1 // wrong
  nPThreadsPerCore = cpuid_logical_per_package / cpuid_cores_per_package = 2

  nLDiesPerPackage = core_count / nCoresSharingLLC = 1
  nPDiesPerPackage = cpuid_cores_per_package / (maxSharingLLC / nPThreadsPerCore) = 1

  nLCoresPerDie    = nCoresSharingLLC = 10
  nPCoresPerDie    = maxSharingLLC / nPThreadsPerCore = 64

  nLThreadsPerDie = nLThreadsPerCore * nLCoresPerDie = 10 // wrong
  nPThreadsPerDie = nPThreadsPerCore * nPCoresPerDie = 128

  nLCoresPerPackage = nLCoresPerDie * nLDiesPerPackage = 10 // wrong
  nPCoresPerPackage = nPCoresPerDie * nPDiesPerPackage = 64

  nLThreadsPerPackage = nLThreadsPerCore * nLCoresPerPackage = 10 // wrong
  nPThreadsPerPackage = nPThreadsPerCore * nPCoresPerPackage = 128 // wrong

  pkg_num = cpu_phys_number / nPThreadsPerPackage = 0

  Normal error:
    10 threads but 16 registered from MADT (patchless).
  After patching MSR 35h read to 20 threads:
    20 threads but 16 registered from MADT (after thread_count patch -> 20)
  To workaround this the code below registers a disabled hyper thread for each efficient CPU.
  We use the fact efficient CPUs are added to the end of MADT by Intel.
*/

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

static cpu_id_t cpuIds[128];
static uint32_t efficientCoreCount;
static uint32_t efficientCoreStart;
static uint32_t efficientCoreRegisterd;

extern "C" unsigned int real_ncpus;
extern "C" kern_return_t ml_processor_register(cpu_id_t cpu_id, uint32_t lapic_id, processor_t *processor_out, boolean_t boot_cpu, boolean_t start);

static uintptr_t org_ml_processor_register;
kern_return_t my_ml_processor_register(cpu_id_t cpu_id, uint32_t lapic_id, processor_t *processor_out, boolean_t boot_cpu, boolean_t start) {
    DBGLOG("cts", "registering their %u boot %d - curr n %u", lapic_id, start, real_ncpus);
    auto kret = FunctionCast(my_ml_processor_register, org_ml_processor_register)(cpu_id, lapic_id, processor_out, boot_cpu, start);
    if (real_ncpus > efficientCoreStart && kret == KERN_SUCCESS && start == FALSE) {
        DBGLOG("cts", "registering %u - curr n %u", lapic_id + 1, real_ncpus);
        processor_t proc;
        PANIC_COND(efficientCoreRegisterd >= arrsize(cpuIds), "cts", "too many efficientCoreRegisterd");
        auto kr = FunctionCast(my_ml_processor_register, org_ml_processor_register)(&cpuIds[efficientCoreRegisterd], lapic_id + 1, &proc, false, false);
        PANIC_COND(kr != KERN_SUCCESS, "cts", "failed to ml_processor_register %u - %d", lapic_id + 1, kr);
        efficientCoreRegisterd++;
    }

    return kret;
}

IOService *CpuTopologySync::probe(IOService *provider, SInt32 *score) {
    if (!IOService::probe(provider, score)) return nullptr;
    if (!provider) return nullptr;

    OSNumber *cpuNumber = OSDynamicCast(OSNumber, provider->getProperty("processor-index"));
    if (!cpuNumber || cpuNumber->unsigned32BitValue() != 0) return nullptr;

    static bool done = false;
    if (!done) {
        lilu_get_boot_args("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
        ADDPR(debugEnabled) = checkKernelArgument("-ctsdbg") || checkKernelArgument("-liludbgall");
        
        done = true;

        uint64_t tc = rdmsr64(MSR_CORE_THREAD_COUNT);
        uint32_t threadCount = tc & 0xFFFFU;
        uint32_t coreCount   = (tc & 0xFFFF0000U) >> 16U;

        DBGLOG("cst", "threadCount %u coreCount %u", threadCount, coreCount);

        // Ignore if HT is disabled.
        if (threadCount == coreCount)
            return nullptr;
        
        // Ignore if EP cores are disabled.
        if (coreCount * 2 <= threadCount)
            return nullptr;
        
        efficientCoreCount = coreCount * 2 - threadCount;
        efficientCoreStart = threadCount - efficientCoreCount;
        
        DBGLOG("cst", "efficientCoreCount %u efficientCoreStart %u", efficientCoreCount, efficientCoreStart);
       
        KernelPatcher &p = lilu.getKernelPatcher();
        org_ml_processor_register = p.routeFunction(reinterpret_cast<uintptr_t>(ml_processor_register),
                                                    reinterpret_cast<uintptr_t>(my_ml_processor_register),
                                                    true, true, false);
        p.clearError();
        PANIC_COND(org_ml_processor_register == 0, "cts", "failed to route _ml_processor_register");
        
        setProperty("VersionInfo", kextVersion);
        return this;
    }

    return nullptr;
}
