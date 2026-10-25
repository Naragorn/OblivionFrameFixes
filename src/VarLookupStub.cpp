#include "VarLookupStub.h"

#include "CodeWriter.h"

namespace framefixes {

UInt32 BuildVarLookupStub(UInt8* buffer, UInt32 capacity, UInt32 stubAddress, UInt32 lookup,
                          UInt32 returnAddress) {
	CodeWriter code(buffer, capacity, stubAddress);
	code.MoveZeroExtendEcxFromEbpWord(0x08);
	code.PushEcx();
	code.PushEax();
	code.CallRelative(lookup);
	code.AddStackPointer(0x08);
	code.MoveEdxFromEax();
	code.JumpRelative(returnAddress);
	return code.Overflowed() ? 0 : code.Size();
}

}  // namespace framefixes
