//////////////////////////////////////////////////////////////////////////////
// This file is part of the Vulkan Assignment								// 
// Copyright ?2020-2022 Prime Zeng                                          // 
////////////////////////////////////////////////////////////////////////////// 

#pragma  once
#include <string>
#include <imgui.h>
#include <memory>
#include <vector>

#include "EditorWindow.h"
#include "FileSystem/File.h"

namespace Maple 
{
	struct FileInfo
	{
		std::string fileName;
		std::string fileType;
		std::string absolutePath;
		bool isFile;
		FileType type;
	};

	class AssetsWindow : public EditorWindow
	{
	public:
		AssetsWindow();
		virtual void onImGui() override;

		auto drawFile(int32_t dirIndex, bool folder, int32_t shownIndex, bool gridView) -> bool;
		auto drawFolder(const FileInfo & dirInfo) -> void;
		auto renderNavigationBar() -> void;
		auto renderBottom() -> void;
		auto getDirectories(const std::string& path) -> void;
		auto popupWindow() -> void;

		inline auto getParsedAssetID(const std::string& extension)
		{
			for (int i = 0; i < assetTypes.size(); i++)
			{
				if (extension == assetTypes[i])
				{
					return i;
				}
			}
			return -1;
		}

		static auto parseFilename(const std::string& str, const char delim, std::vector<std::string>& out) -> std::string;
		static auto parseFiletype(const std::string& filename)->std::string;
		static auto getFsContents(const std::string& path) ->std::vector<FileInfo>;
		static auto readDirectory(const std::string& path)->std::vector<FileInfo>;
		static auto getParentPath(const std::string& path) -> std::string;


		static auto searchFiles(const std::string& query) -> std::vector<std::string>;
		static auto move(const std::string& filePath, const std::string& movePath) -> bool;

		static auto stripExtras(const std::string& filename) -> std::string;

	private:

		static inline std::vector<std::string> assetTypes = {
			"fbx", "obj", "wav", "mp3","png","jpg","bmp", "scene", "ogg", "lua" };

		std::string currentDirPath;
		std::string baseDirPath;
		std::string previousDirPath;
		std::string movePath;
		std::string lastNavPath;
		static std::string delimiter;

		std::string directories[100];
		int32_t directoryCount;

		size_t basePathLen;
		bool isDragging = false;
		bool isInListView = true;
		bool updateBreadCrumbs = true;
		bool showHiddenFiles = false;
		int32_t gridItemsPerRow;

		ImGuiTextFilter filter;

		char* inputText = nullptr;
		char* inputHint = nullptr;
		char inputBuffer[1024];

		std::vector<FileInfo> currentDir;
		std::vector<FileInfo> baseProjectDir;
		std::vector<std::string> splitPath;

	};
};