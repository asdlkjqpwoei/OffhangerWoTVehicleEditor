#include "EditorWindow.h"

namespace OffHanger::Ui
{
using namespace winrt;

IAsyncAction EditorWindow::RestoreSession()
{
	auto Lifetime = shared_from_this();
	if (Working || RestoringSession)
		co_return;
	RestoringSession = true;
	try
	{
		auto Baseline = ApplicationFolder() / L"script.pkg";
		if (!NativeDefaults && FileSystem::is_regular_file(Baseline))
		{
			SetWorking(true, "Session.Loading");
			apartment_context UiThread;
			std::shared_ptr<DefaultResources const> Loaded;
			std::exception_ptr Failure;
			co_await resume_background();
			try
			{
				Loaded = std::make_shared<DefaultResources>(Baseline);
			}
			catch (...)
			{
				Failure = std::current_exception();
			}
			co_await UiThread;
			SetWorking(false);
			if (Failure)
				std::rethrow_exception(Failure);
			NativeDefaults = std::move(Loaded);
			BuildShell();
		}
		if (NativeDefaults && !CloseRequested)
		{
			if (!Preferences.GameFolder.empty())
				co_await ScanGame();
			if (!CloseRequested)
			{
				auto Save = Preferences.LastSave.empty() ? DefaultSavePath() : Preferences.LastSave;
				if (FileSystem::is_regular_file(Save))
					co_await OpenSavePath(Save);
				else if (!Preferences.LastSave.empty())
					SetStatus(Tr("Session.SaveMissing") + HString(PathUtf8(Save)));
			}
		}
	}
	catch (std::exception const& Error)
	{
		SetWorking(false);
		BaselineError = Error.what();
		if (!NativeDefaults)
			BuildShell();
		SetStatus(HString(Error.what()));
		Log(Error.what());
	}
	catch (hresult_error const& Error)
	{
		SetWorking(false);
		SetStatus(Error.message());
		Log(String(Error.message()));
	}
	RestoringSession = false;
	if (CloseRequested)
		RequestClose();
}

IAsyncAction EditorWindow::OpenSavePath(FileSystem::path Path)
{
	auto Lifetime = shared_from_this();
	if (Working)
		co_return;
	SetWorking(true, "Session.LoadingSave");
	apartment_context UiThread;
	SaveSession Loaded;
	std::exception_ptr Failure;
	co_await resume_background();
	try
	{
		Loaded.Open(Path);
	}
	catch (...)
	{
		Failure = std::current_exception();
	}
	co_await UiThread;
	SetWorking(false);
	if (Failure)
		std::rethrow_exception(Failure);
	SaveData = std::move(Loaded);
	Preferences.LastSave = Path;
	if (!GameRunning())
		Preferences.Save();
	RefreshSave();
	SetStatus(Tr("Save.SaveLoaded"));
}
} // namespace OffHanger::Ui
