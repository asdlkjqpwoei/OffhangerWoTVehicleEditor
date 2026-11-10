#include "EditorWindow.h"

namespace OffHanger::Ui
{
using namespace winrt;

IAsyncAction EditorWindow::ChangeGameFiles(bool Reset)
{
	auto Lifetime = shared_from_this();
	if (Working)
		co_return;
	try
	{
		RequireTextEdits();
		if (GameCatalog.Vehicles.empty())
			throw std::runtime_error(String(Tr("Vehicle.LoadFirst")));
		if (!Reset && !XmlDrafts.empty())
			throw std::runtime_error(String(Tr("Xml.ApplyBeforeSave")));
		SetWorking(true, "Progress.validate");
		CancelWork = false;
		apartment_context UiThread;
		std::vector<std::string> Paths;
		std::exception_ptr Failure;
		if (!Reset)
		{
			co_await resume_background();
			try
			{
				Paths = VehicleData.PreviewSave();
			}
			catch (...)
			{
				Failure = std::current_exception();
			}
			co_await UiThread;
			if (Failure)
				std::rethrow_exception(Failure);
		}
		if (!Reset && Paths.empty())
			SetWorking(false, "Vehicle.NothingToSave");
		else
		{
			auto Description = String(Reset ? Tr("Vehicle.ResetAllWarning") : Tr("Vehicle.ApplyWarning"));
			Description += "\n" + PathUtf8(Preferences.OverrideFolder) + "\n";
			if (Reset)
				Description += std::to_string(VehicleData.AppliedCount());
			else
				for (auto const& Path : Paths)
					Description += "\n" + Path;
			bool Accepted =
				!CloseRequested &&
				co_await Confirm(Tr(Reset ? "Vehicle.ResetAll" : "Vehicle.Save"), HString(Description));
			if (!Accepted || CloseRequested)
				SetWorking(false, "General.Ready");
			else
			{
				DeactivateArmor();
				WorkProgress.IsIndeterminate(false);
				WorkProgress.Value(0);
				auto Queue = Root.DispatcherQueue();
				auto Report = [Lifetime, Queue](std::string const& Stage, size_t Completed, size_t Total)
				{
					Queue.TryEnqueue(
						[Lifetime, Stage, Completed, Total]
						{
							if (!Lifetime->Working)
								return;
							Lifetime->WorkProgress.IsIndeterminate(false);
							Lifetime->WorkProgress.Value(Total ? 100.0 * Completed / Total : 0);
							auto Caption = Lifetime->Tr(("Progress." + Stage).c_str());
							Lifetime->SetStatus(
								Caption + L" · " +
								HString(std::to_string(Completed) + " / " + std::to_string(Total)));
						});
				};
				co_await resume_background();
				try
				{
					if (Reset)
						VehicleData.ResetAll(Report);
					else
						VehicleData.SaveToGame(Report);
				}
				catch (...)
				{
					Failure = std::current_exception();
				}
				co_await UiThread;
				SetWorking(false);
				if (Failure)
					std::rethrow_exception(Failure);
				if (Reset)
					XmlDrafts.clear();
				ActivateVehicleTab(CurrentVehicleTab);
				auto Status = Tr(Reset ? "Vehicle.ResetDone" : "Vehicle.Saved");
				auto const& Cache = VehicleData.LastCacheRefresh();
				if (Cache.Method == GameCacheMethod::Invalidated)
					Status = Reset ? Status + L" · " + Tr("Cache.Invalidated") : Tr("Cache.Invalidated");
				SetStatus(Status);
				if (!Cache.Detail.empty())
					Log(Cache.Detail);
			}
		}
	}
	catch (...)
	{
		SetWorking(false);
		Run([] { throw; });
	}
	if (CloseRequested)
		RequestClose();
}
} // namespace OffHanger::Ui
