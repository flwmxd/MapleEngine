//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

namespace maple
{
	enum class EditorState
	{
		Paused,
		Play,
		Next,
		Preview
	};

	namespace component
	{
		struct AppState 
		{
			EditorState	state = EditorState::Play;
		};
	}
}