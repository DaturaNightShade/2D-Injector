#include "be_logging.h"
#include "hooks.h"
#include "logging.h"
#include "disassembly.h"
#include "dbg_symbols.h"

struct AddressInfo
{
	AddressInfo(void* _address) : address(_address)
	{
		symbol = Symbols::GetSymFromAddr((uintptr_t)address);

		dll_name_address.second = Utils::ModuleFromAddress((uintptr_t)address, &dll_name_address.first);
	}

	void Print()
	{
		if (!symbol.empty())
		{
			Logger::Get()->Print(
				COLOR_ID::none, "%wZ!%s (0x%02x) \n", &dll_name_address.first, symbol.c_str(), address);
		}
		else if (dll_name_address.second)
		{
			Logger::Get()->Print(COLOR_ID::none, "%wZ+0x%02x \n",
				&dll_name_address.first, (uintptr_t)address - (uintptr_t)dll_name_address.second);
		}
		else
		{
			Logger::Get()->Print(COLOR_ID::none, "0x%02x \n", address);
		}
	}

	std::string Format()
	{
		char buffer[256];

		if (!symbol.empty())
		{
			Logger::Get()->Format(
				buffer, "%wZ!%s (0x%02x) \n", &dll_name_address.first, symbol.c_str(), address);
		}
		else if (dll_name_address.second)
		{
			Logger::Get()->Format(buffer, 
				"%wZ+0x%02x \n", &dll_name_address.first, (uintptr_t)address - (uintptr_t)dll_name_address.second);
		}
		else
		{
			Logger::Get()->Format(buffer, "0x%02x \n", address);
		}

		return std::string{ buffer };
	}



	void* address;
	std::pair<UNICODE_STRING, void*> dll_name_address;
	std::string	symbol;
};


/*	log out-of-module function calls and jmps		*/

void ExecuteHook(GuestRegisters* registers, void* return_address, void* o_guest_rip)
{
	AddressInfo retaddr_info = { return_address };
	AddressInfo rip_info = { o_guest_rip };

	Logger::Get()->Print(COLOR_ID::green, "[EXECUTE]\n");  
	Logger::Get()->Print(COLOR_ID::none, "return address = ");

	retaddr_info.Print();

	Logger::Get()->Print(COLOR_ID::none, "RIP = ", o_guest_rip);

	rip_info.Print();

	Logger::Get()->Print(COLOR_ID::none, "\n\n");
}


/*	log specific reads and writes		*/

void ReadWriteHook(GuestRegisters* registers, void* o_guest_rip)
{
	AddressInfo rip_info = { o_guest_rip };

	ZydisDecodedOperand operands[5] = { 0 };

	auto instruction = Disasm::Disassemble((uint8_t*)o_guest_rip, operands);

	Logger::Get()->Print(COLOR_ID::magenta, "[READ/WRITE]\n");
	Logger::Get()->Print(COLOR_ID::none, "RIP = ");

	rip_info.Print();

	ZydisRegisterContext context;

	Disasm::MyRegContextToZydisRegContext(registers, &context, o_guest_rip);

	for (int i = 0; i < instruction.operand_count_visible; ++i)
	{
		auto mem_target = Disasm::GetMemoryAccessTarget(
			instruction, &operands[i], (ZyanU64)o_guest_rip, &context);

		if (operands[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)
		{
			Logger::Get()->Print(COLOR_ID::none, "[write => 0x%02x]\n", mem_target);
		}
		else if (operands[i].actions & ZYDIS_OPERAND_ACTION_MASK_READ)
		{
			Logger::Get()->Print(COLOR_ID::none, "[read => 0x%02x]\n", mem_target);
		}
	}

	Logger::Get()->Print(COLOR_ID::none, "\n\n");
}

std::vector<BranchLog::LogEntry> traced_branches;

void BranchLogFullHook()
{
	traced_branches.insert(traced_branches.end(),
		BVM::log_buffer->info.buffer, BVM::log_buffer->info.buffer + BVM::log_buffer->info.buffer_idx);
}

void BranchTraceFinished()
{
	for (auto entry : traced_branches)
	{
		Utils::LogToFile(LOG_FILE, "branch %s -> %s", 
			AddressInfo{ (void*)entry.branch_address }.Format().c_str(),
			AddressInfo{ (void*)entry.branch_address }.Format().c_str()
		);
	}
}

void StartBELogger()
{
	Disasm::Init();
	Symbols::Init();

	Logger::Get()->Print(COLOR_ID::magenta, 
		"Symbols::GetSymFromAddr((uintptr_t)GetProcAddress); %s \n", Symbols::GetSymFromAddr((uintptr_t)GetProcAddress).c_str());

	/*	Only allow BEClient pages to execute in 3rd NCR3 	*/

	uintptr_t beclient = NULL;

	while (!beclient)
	{
		Sleep(500);
		beclient = (uintptr_t)GetModuleHandle(L"BEService.exe");
	}

	BVM::InstrumentationHook(BVM::sandbox_readwrite, ReadWriteHook);
	BVM::InstrumentationHook(BVM::sandbox_execute, ExecuteHook);
	BVM::InstrumentationHook(BVM::branch_log_full, BranchLogFullHook);
	BVM::InstrumentationHook(BVM::branch_trace_finished, BranchTraceFinished);

	__debugbreak();

	BVM::SandboxRegion(beclient, PeHeader(beclient)->OptionalHeader.SizeOfImage);

	auto kernel32 = (uint8_t*)GetModuleHandleA("kernel32.dll");

	BVM::DenySandboxMemAccess(kernel32 + 0x1005);

	BVM::TraceFunction(
		(uint8_t*)GetModuleHandleA(NULL) + 0x1080, beclient, PeHeader(beclient)->OptionalHeader.SizeOfImage);
}
