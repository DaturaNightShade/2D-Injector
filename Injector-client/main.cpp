#include "communicate.h"
#include "manual_map.h"
#include "utils.h"
#include <iostream>
#include "security.h"

extern "C" __declspec(dllexport) int InjectDLLBytes(int32_t pid, uint8_t* dll_raw, const char* entrypoint_name)
{
	if (*(int32_t*)dll_raw != INJECTOR_PASSWORD)
	{
		SecurityViolation();
	}
	else
	{
		*(int32_t*)dll_raw = 0x5A4D;
	}

	Driver::Init();

	auto image_real_size = PeHeader(dll_raw)->OptionalHeader.SizeOfImage;

	auto alloc_size = image_real_size + PAGE_SIZE;

	auto alloc_base = Driver::AllocateMemory(pid, alloc_size);

	auto cheat_base = alloc_base + PAGE_SIZE;

	/*	local copy of cheat dll	*/

	uint8_t* cheat_mapped = NULL;

	PE::RemapImage(dll_raw, &cheat_mapped, pid, cheat_base);

	std::cout << std::hex << " cheat_base 0x" << cheat_base << std::endl;

	auto pe_hdr = PeHeader(cheat_mapped);

	/*	write the dll	*/

	Driver::WriteMem(pid, cheat_base, cheat_mapped, image_real_size);

	/*	hide .text	*/

	auto section = (IMAGE_SECTION_HEADER*)(pe_hdr + 1);

	//for (int i = 0; i < pe_hdr->FileHeader.NumberOfSections; ++i)
	//{
	//	if (section[i].Characteristics & IMAGE_SCN_CNT_CODE)
	//	{
	//		std::cout << section[i].Name << std::endl;

	//		/*	randomize .text bytes	*/

	//		for (auto byte = cheat_mapped + section[i].VirtualAddress;
	//			byte < cheat_mapped + section[i].VirtualAddress + section[i].SizeOfRawData;
	//			byte += 8)
	//		{ 
	//			uint64_t value = 0xCCFFDDEE; 
	//			Driver::WriteMem(pid, cheat_base + (byte - cheat_mapped), (BYTE*)&value, 8);
	//		}

	//		for (auto page = cheat_mapped + section[i].VirtualAddress;
	//			page < cheat_mapped + section[i].VirtualAddress + section[i].SizeOfRawData;
	//			page += PAGE_SIZE)
	//		{
	//			Driver::SetNptHook(pid, PAGE_SIZE, cheat_base + (page - cheat_mapped), page);
	//		}
	//	}
	//}


	auto entry_point = (uintptr_t)PE::GetExport((uintptr_t)cheat_mapped, entrypoint_name);

	std::cout << std::hex << " entry_point 0x" << entry_point + cheat_base << std::endl;

	Driver::InvokeRemoteFunc(entry_point + cheat_base, pid, alloc_base, image_real_size);

	Driver::ExitDriver();

	return 0;
}

#define ENTRYPOINT_NAME "HookEntryPoint"

extern "C" int main()
{
	//system("kdmapper C:\\Users\\cppco\\OneDrive\\Desktop\\testing_drivers\\AMD-Hypervisor.sys");
	//system("kdmapper C:\\Users\\cppco\\OneDrive\\Desktop\\testing_drivers\\injector-driver.sys");
	
	std::string cheat_dll_name;

	std::cout << "Enter the name of the DLL to inject: " << std::endl;
	std::cin >> cheat_dll_name;


	std::wstring target_process;

	std::cout << "Enter the name of the target process: " << std::endl;
	std::wcin >> target_process;

	auto target_pid = Util::GetProcId(target_process.c_str());

	uint8_t* cheat_dll_raw = NULL;

	auto image_size = Util::LoadFileIntoMemory(cheat_dll_name.c_str(), &cheat_dll_raw);

	InjectDLLBytes(target_pid, cheat_dll_raw, ENTRYPOINT_NAME);
	std::cin.get();
}
