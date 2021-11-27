//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma  once
#include <string>
#include <imgui.h>
#include <memory>

#include "EditorWindow.h"
#include <ImGuiEnttEditor.hpp>


namespace Maple 
{
	
	class PropertiesWindow : public EditorWindow
	{
	public:
		PropertiesWindow();
		virtual auto onImGui() -> void override;
		virtual auto onSceneCreated(Scene* scene) -> void override;

	private:
		bool init = false;
		MM::EntityEditor<entt::entity> enttEditor;
	};
};