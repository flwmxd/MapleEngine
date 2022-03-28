//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once
#include <string>
#include <vector>
#include <memory>
#include "Engine/Core.h"
#include "FileSystem/IResource.h"

namespace maple
{
	class MAPLE_EXPORT AnimationController : public IResource
	{
	public:
		AnimationController(const std::string& path) :path(path) {};
		virtual auto getResourceType() const->FileType { return FileType::AnimCtrl; }
		virtual auto getPath() const->std::string { return path; }
	private:
		std::string path;
	};
};