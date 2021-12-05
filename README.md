# CpuTopologySync
[![Build Status](https://github.com/acidanthera/CpuTopologySync/workflows/CI/badge.svg?branch=master)](https://github.com/acidanthera/CpuTopologySync/actions) [![Scan Status](https://scan.coverity.com/projects/XXXX/badge.svg?flat=1)](https://scan.coverity.com/projects/XXXX)

Activate efficient cores in Alder Lake CPUs on macOS. The kext additionally requires a kernel patch:

```
Base:    _cpuid_set_info
Find:    B9 35 00 00 00 0F 32
Replace: B8 14 00 0A 00 31 D2
Count:   2
Comment: Set core_count = 0x0A, thread_count = 0x14
Identifier: kernel
```

Here `0x0A` and `0x14` are effective core count and thread count. For any Alder Lake CPU the amount of
threads should be twice bigger than the amount of cores. Therefore, while for the i5 the values
are `0x0A` and `0x14` for an i9 the values will be `0x10` and `0x20` correspondingly.

Enabling efficient cores additionally lowers CPU ring buffer frequency and disables AVX-512.
Furthermore, macOS scheduler is not optimised for the asymmetric CPU topology. For these reasons
real-world performance may often be lower even compared to the configurations with efficient cores
completely disabled, single thread performance in particular.

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for writing the software and maintaining it
