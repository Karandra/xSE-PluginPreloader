#pragma once
#include "Framework.hpp"
#include <kxf/Application/CoreApplication.h>
#include <kxf/Localization/WindowsLocalizationPackage.h>

namespace xSE
{
	class PreloadHandler;
}

namespace xSE
{
	class Application final: public kxf::RTTI::Implementation<Application, kxf::CoreApplication>
	{
		private:
			PreloadHandler& m_PreloadHandler;
			kxf::WindowsLocalizationPackage m_LocalizationPackage;
			
		public:
			Application(PreloadHandler& handler);
			~Application();

		public:
			// ICoreApplication
			bool OnCreate() override;
			bool OnInit() override;

			bool OnMainLoopException() override;
			void OnUnhandledException() override;

			const kxf::ILocalizationPackage& GetLocalizationPackage() const override
			{
				return m_LocalizationPackage;
			}
	};
}
