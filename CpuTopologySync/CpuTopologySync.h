/** @file
  CpuTopologySync.

  Copyright (c) 2021, vit9696. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <IOKit/IOService.h>

class CpuTopologySync : public IOService {
    OSDeclareDefaultStructors(CpuTopologySync)
public:
    virtual IOService* probe(IOService* provider, SInt32* score) override;
};
