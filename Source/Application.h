#pragma once
#include "Framework.hpp"
#include <kxf/Application/GUIApplication.h>
#include <kxf/Localization/AndroidLocalizationPackage.h>

namespace xSE
{
	class PreloadHandler;
}

namespace xSE
{
	class Application: public kxf::RTTI::Implementation<Application, kxf::GUIApplication>
	{
		private:
			PreloadHandler& m_PreloadHandler;
			kxf::AndroidLocalizationPackage m_LocalizationPackage;
			
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
