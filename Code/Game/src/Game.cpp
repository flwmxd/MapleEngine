//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#include "Main.h"
#include "Application.h"
#include "FileSystem/File.h"

namespace Maple 
{
	class Game : public Application
	{
	public:
		Game(): Application(new DefaultDelegate()){}
		auto init() -> void override 
		{
			Application::init();
			if (File::fileExists("default.scene")) {
				sceneManager->addSceneFromFile("default.scene");
				sceneManager->switchScene("default.scene");
			}
		};
	};
};



maple::Application* createApplication()
{
	return new maple::Game();
}