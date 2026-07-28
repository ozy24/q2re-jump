// [Jump] Data directory resolution and safe file writes.
//
// game_import_t exposes no file I/O, so the mod uses the C++ standard library
// directly. Paths are resolved relative to the DLL itself rather than the
// process working directory, which is what actually lands the data next to
// game_x64.dll on a listen server.

#include "../g_local.h"
#include "jump_local.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <windows.h>

static std::filesystem::path jump_data_root;
static bool					 jump_data_root_resolved = false;

static std::filesystem::path Jump_ResolveDllDirectory()
{
	HMODULE module = nullptr;

	// Any address inside this DLL identifies the module.
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						   (LPCSTR) &Jump_ResolveDllDirectory, &module) &&
		module)
	{
		char path[MAX_PATH] = { 0 };

		if (GetModuleFileNameA(module, path, sizeof(path)))
			return std::filesystem::path(path).parent_path();
	}

	return std::filesystem::current_path();
}

const std::filesystem::path &Jump_DataRoot()
{
	if (jump_data_root_resolved)
		return jump_data_root;

	jump_data_root_resolved = true;

	if (jump_data_dir && jump_data_dir->string && jump_data_dir->string[0])
		jump_data_root = std::filesystem::path(jump_data_dir->string);
	else
		jump_data_root = Jump_ResolveDllDirectory() / "jump";

	std::error_code ec;
	std::filesystem::create_directories(jump_data_root, ec);

	if (ec)
		gi.Com_PrintFmt("[jump] could not create data directory {}: {}\n", jump_data_root.string(), ec.message());
	else
		Jump_Log("data directory: %s", jump_data_root.string().c_str());

	return jump_data_root;
}

std::filesystem::path Jump_MapTimesPath(const char *mapname)
{
	const std::filesystem::path dir = Jump_DataRoot() / "maptimes";

	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	return dir / (jump::SafeName(mapname ? mapname : "") + ".json");
}

bool Jump_ReadFile(const std::filesystem::path &path, std::string &out)
{
	std::ifstream in(path, std::ios::binary);

	if (!in)
		return false;

	std::ostringstream buffer;
	buffer << in.rdbuf();
	out = buffer.str();

	return true;
}

// Write via a temporary file and rename, so a crash or an Alt-F4 mid-write
// can never leave a half-written records file behind.
bool Jump_WriteFileAtomic(const std::filesystem::path &path, const std::string &contents)
{
	std::filesystem::path tmp = path;
	tmp += ".tmp";

	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);

		if (!out)
		{
			gi.Com_PrintFmt("[jump] could not open {} for writing\n", tmp.string());
			return false;
		}

		out << contents;

		if (!out)
		{
			gi.Com_PrintFmt("[jump] write failed for {}\n", tmp.string());
			return false;
		}
	}

	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);

	if (ec)
	{
		// rename() will not clobber on some filesystems; fall back to a
		// remove-then-rename before giving up.
		std::filesystem::remove(path, ec);
		std::filesystem::rename(tmp, path, ec);
	}

	if (ec)
	{
		gi.Com_PrintFmt("[jump] could not replace {}: {}\n", path.string(), ec.message());
		std::filesystem::remove(tmp, ec);
		return false;
	}

	return true;
}
