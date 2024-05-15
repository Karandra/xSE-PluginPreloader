#include "pch.hpp"
#include "Application.h"
#include "xSEPluginPreloader.h"
#include <kxf/System/DynamicLibrary.h>
#include <kxf/System/DynamicLibraryEvent.h>

namespace xSE
{
	Application::Application(PreloadHandler& handler)
		:m_PreloadHandler(handler)
	{
		ICoreApplication::SetInstance(this);
	}
	Application::~Application()
	{
		ICoreApplication::SetInstance(nullptr);
	}

	bool Application::OnCreate()
	{
		SetName(m_PreloadHandler.GetLibraryName());
		SetVersion(m_PreloadHandler.GetLibraryVersion());
		SetVendorName("Karandra");

		return true;
	}
	bool Application::OnInit()
	{
		Bind(kxf::DynamicLibraryEvent::EvtLoaded, [&](kxf::DynamicLibraryEvent& event)
		{
			kxf::Log::Trace("Library '{}' loaded from '{}'", event.GetBaseName().GetFullPath(), event.GetLibrary().GetFilePath().GetFullPath());
		}, kxf::BindEventFlag::AlwaysSkip);
		Bind(kxf::DynamicLibraryEvent::EvtUnloaded, [&](kxf::DynamicLibraryEvent& event)
		{
			kxf::Log::Trace("Library '{}' unloaded", event.GetBaseName().GetFullPath());
		}, kxf::BindEventFlag::AlwaysSkip);

		return true;
	}

	bool Application::OnMainLoopException()
	{
		kxf::String exceptionMessage;
		try
		{
			throw;
		}
		catch (const std::exception& e)
		{
			exceptionMessage = e.what();
		}
		catch (const char* e)
		{
			exceptionMessage = e;
		}
		catch (...)
		{
			exceptionMessage = "unknown error";
		}

		// Log error message
		kxf::Log::Critical("Unexpected exception has occurred: '{}', the program will terminate", exceptionMessage);

		// Exit the main loop and terminate the program
		return false;
	}
	void Application::OnUnhandledException()
	{
		Application::OnMainLoopException();
	}
}
