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
			m_PreloadHandler.Log("<Framework:App> Library '{}' loaded from '{}'", event.GetBaseName().GetFullPath(), event.GetLibrary().GetFilePath().GetFullPath());
		}, kxf::BindEventFlag::AlwaysSkip);
		Bind(kxf::DynamicLibraryEvent::EvtUnloaded, [&](kxf::DynamicLibraryEvent& event)
		{
			m_PreloadHandler.Log("<Framework:App> Library '{}' unloaded", event.GetBaseName().GetFullPath());
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
		m_PreloadHandler.Log("<Framework:App> Unexpected exception has occurred: {}.\r\n\r\nThe program will terminate.", exceptionMessage);

		// Exit the main loop and terminate the program
		return false;
	}
	void Application::OnUnhandledException()
	{
		Application::OnMainLoopException();
	}
	void Application::OnAssertFailure(const kxf::String& file, int line, const kxf::String& function, const kxf::String& condition, const kxf::String& message)
	{
		m_PreloadHandler.Log("<Framework:App> Assert failure: File=[{}]@{}; Function=[{}]; Condition=[{}]; Message=[{}]", file, line, function, condition, message);
	}
}
