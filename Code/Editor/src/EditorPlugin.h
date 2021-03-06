//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once
#include <memory>
#include <functional>


namespace maple
{
	class Editor;

	class EditorPlugin 
	{
	public:
		virtual auto process(Editor* editor) -> void = 0;
        inline auto& isInited() const { return inited; }
        inline auto setInited(bool inited) { this->inited = inited; }
	protected:
		bool inited = false;

	};

	class FunctionalPlugin : public EditorPlugin
	{
	public:
		FunctionalPlugin(const std::function<void(Editor*)>& initCallback) : callback(initCallback) {}
		auto process(Editor* editor) -> void override { if(callback) callback(editor); }
        inline auto setCallback(std::function<void(Editor*)> callback) { this->callback = callback; }
	private:
		std::function<void(Editor*)> callback;
	};
};