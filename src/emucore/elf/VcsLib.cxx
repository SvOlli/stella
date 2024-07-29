//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2024 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include "VcsLib.hxx"

#include "BusTransactionQueue.hxx"
#include "ElfEnvironment.hxx"
#include "exception/FatalEmulationError.hxx"
#include "ElfEnvironment.hxx"

using namespace elfEnvironment;

namespace {
  CortexM0::err_t memset(uInt32 target, uInt8 value, uInt32 size, CortexM0& cortex)
  {
    const uInt16 value16 = value | (value << 16);
    const uInt32 value32 = value16 | (value16 << 16);
    CortexM0::err_t err;
    uInt32 ptr = target;

    while (ptr < target + size) {
      if ((ptr & 0x03) == 0 && size - (ptr - target) >= 4) {
        err = cortex.write32(ptr, value32);
        ptr += 4;
      }
      else if ((ptr & 0x01) == 0 && size - (ptr - target) >= 2) {
        err = cortex.write16(ptr, value16);
        ptr += 2;
      }
      else {
        err = cortex.write8(ptr, value);
        ptr++;
      }

      if (err) return err;
    }

    return 0;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
VcsLib::VcsLib(BusTransactionQueue& transactionQueue) : myTransactionQueue(transactionQueue)
{}

void VcsLib::reset()
{
  myStuffMaskA = myStuffMaskX = myStuffMaskY = 0x00;
  myIsWaitingForRead = false;
  myWaitingForReadAddress = 0;
  myCurrentAddress = 0;
  myCurrentValue = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void VcsLib::vcsWrite5(uInt8 zpAddress, uInt8 value)
{
	myTransactionQueue
    .injectROM(0xa9)
	  .injectROM(value)
	  .injectROM(0x85)
	  .injectROM(zpAddress)
    .yield(zpAddress);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void VcsLib::vcsCopyOverblankToRiotRam()
{
  for (uInt8 i = 0; i < OVERBLANK_PROGRAM_SIZE; i++)
    vcsWrite5(0x80 + i, OVERBLANK_PROGRAM[i]);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void VcsLib::vcsStartOverblank()
{
	myTransactionQueue
    .injectROM(0x4c)
	  .injectROM(0x80)
	  .injectROM(0x00)
    .yield(0x0080);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void VcsLib::vcsEndOverblank()
{
  myTransactionQueue
    .injectROMAt(0x00, 0x1fff)
    .yield(0x00ac)
    .setNextInjectAddress(0x1000);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void VcsLib::vcsNop2n(uInt16 n)
{
  if (n == 0) return;

  myTransactionQueue
    .injectROM(0xea)
    .setNextInjectAddress(myTransactionQueue.getNextInjectAddress() + n - 1);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void VcsLib::vcsLda2(uInt8 value)
{
        myTransactionQueue
    	  .injectROM(0xa9)
	      .injectROM(value);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CortexM0::err_t VcsLib::fetch16(uInt32 address, uInt16& value, uInt8& op, CortexM0& cortex)
{
  uInt32 arg;
  CortexM0::err_t err;

  if (myTransactionQueue.size() >= elfEnvironment::QUEUE_SIZE_LIMIT)
    return CortexM0::errCustom(ERR_STOP_EXECUTION);

  myTransactionQueue.setTimestamp(cortex.getCycles());

  switch (address) {
    case ADDR_MEMSET:
      err = memset(cortex.getRegister(0), cortex.getRegister(1), cortex.getRegister(3), cortex);
      if (err) return err;

      return returnFromStub(value, op);

    case ADDR_MEMCPY:
      FatalEmulationError::raise("unimplemented: memcpy");

    case ADDR_VCS_LDA_FOR_BUS_STUFF2:
      vcsLda2(myStuffMaskA);
      return returnFromStub(value, op);

    case ADDR_VCS_LDX_FOR_BUS_STUFF2:
      vcsLda2(myStuffMaskX);
      return returnFromStub(value, op);

    case ADDR_VCS_LDY_FOR_BUS_STUFF2:
      vcsLda2(myStuffMaskY);
      return returnFromStub(value, op);

    case ADDR_VCS_WRITE3:
      arg = cortex.getRegister(0);

      myTransactionQueue
        .injectROM(0x85)
        .injectROM(arg)
        .stuffByte(cortex.getRegister(1), arg);

      return returnFromStub(value, op);

    case ADDR_VCS_JMP3:
      myTransactionQueue
    	  .injectROM(0x4c)
        .injectROM(0x00)
        .injectROM(0x10)
        .setNextInjectAddress(0x1000);

      return returnFromStub(value, op);

    case ADDR_VCS_NOP2:
      myTransactionQueue.injectROM(0xea);
      return returnFromStub(value, op);

    case ADDR_VCS_NOP2N:
      vcsNop2n(cortex.getRegister(0));
      return returnFromStub(value, op);

    case ADDR_VCS_WRITE5:
      vcsWrite5(cortex.getRegister(0), cortex.getRegister(1));
      return returnFromStub(value, op);

    case ADDR_VCS_WRITE6:
      FatalEmulationError::raise("unimplemented: vcsWrite6");

    case ADDR_VCS_LDA2:
      vcsLda2(cortex.getRegister(0));
      return returnFromStub(value, op);

    case ADDR_VCS_LDX2:
      FatalEmulationError::raise("unimplemented: vcsLdx2");

    case ADDR_VCS_LDY2:
      FatalEmulationError::raise("unimplemented: vcsLdy2");

    case ADDR_VCS_SAX3:
      FatalEmulationError::raise("unimplemented: vcsSax3");

    case ADDR_VCS_STA3:
      arg = cortex.getRegister(0);

      myTransactionQueue
    	  .injectROM(0x85)
        .injectROM(arg)
        .yield(arg);

      return returnFromStub(value, op);

    case ADDR_VCS_STX3:
      FatalEmulationError::raise("unimplemented: vcsStx3");

    case ADDR_VCS_STY3:
      FatalEmulationError::raise("unimplemented: vcsSty3");

    case ADDR_VCS_STA4:
      FatalEmulationError::raise("unimplemented: vcsSta4");

    case ADDR_VCS_STX4:
      FatalEmulationError::raise("unimplemented: vcsStx4");

    case ADDR_VCS_STY4:
      FatalEmulationError::raise("unimplemented: vcsSty4");

    case ADDR_VCS_COPY_OVERBLANK_TO_RIOT_RAM:
      vcsCopyOverblankToRiotRam();
      return returnFromStub(value, op);

    case ADDR_VCS_START_OVERBLANK:
      vcsStartOverblank();
      return returnFromStub(value, op);

    case ADDR_VCS_END_OVERBLANK:
      vcsEndOverblank();
      return returnFromStub(value, op);

    case ADDR_VCS_READ4:
      if (myIsWaitingForRead) {
        if (myTransactionQueue.size() > 0 || myCurrentAddress != myWaitingForReadAddress)
          return CortexM0::errCustom(ERR_STOP_EXECUTION);

        myIsWaitingForRead = false;
        cortex.setRegister(0, myCurrentValue);

        return returnFromStub(value, op);
      } else {
        arg = cortex.getRegister(0);

        myIsWaitingForRead = true;
        myWaitingForReadAddress = arg;

        myTransactionQueue
          .injectROM(0xad)
	        .injectROM(arg & 0xff)
	        .injectROM(arg >> 8)
          .yield(arg);

        return CortexM0::errCustom(ERR_STOP_EXECUTION);
      }

    case ADDR_RANDINT:
      FatalEmulationError::raise("unimplemented: randint ");

    case ADDR_VCS_TXS2:
      FatalEmulationError::raise("unimplemented: vcsTx2");

    case ADDR_VCS_JSR6:
      FatalEmulationError::raise("unimplemented: vcsJsr6");

    case ADDR_VCS_PHA3:
      FatalEmulationError::raise("unimplemented: vcsPha3");

    case ADDR_VCS_PHP3:
      FatalEmulationError::raise("unimplemented: vcsPph3");

    case ADDR_VCS_PLA4:
      FatalEmulationError::raise("unimplemented: vcsPla4");

    case ADDR_VCS_PLP4:
      FatalEmulationError::raise("unimplemented: vcsPlp4");

    case ADDR_VCS_PLA4_EX:
      FatalEmulationError::raise("unimplemented: vcsPla4Ex");

    case ADDR_VCS_PLP4_EX:
      FatalEmulationError::raise("unimplemented: vcsPlp4Ex");

    case ADDR_VCS_JMP_TO_RAM3:
      FatalEmulationError::raise("unimplemented: vcsJmpToRam3");

    case ADDR_VCS_WAIT_FOR_ADDRESS:
      FatalEmulationError::raise("unimplemented: vcsWaitForAddress");

    case ADDR_INJECT_DMA_DATA:
      FatalEmulationError::raise("unimplemented: vcsInjectDmaData");

    default:
      return CortexM0::errIntrinsic(CortexM0::ERR_UNMAPPED_FETCH16, address);
  }
}

void VcsLib::updateBus(uInt16 address, uInt8 value)
{
  myCurrentAddress = address;
  myCurrentValue = value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CortexM0::err_t VcsLib::returnFromStub(uInt16& value, uInt8& op)
{
  constexpr uInt16 BX_LR = 0x4770;

  value = BX_LR;
  op = CortexM0::decodeInstructionWord(BX_LR);

  return CortexM0::ERR_NONE;
}