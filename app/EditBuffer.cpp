#include "EditorWindow.h"

namespace OffHanger::Ui
{
void EditorWindow::BindDeferredEdit(Controls::TextBox const& Input,
									std::function<bool(Controls::TextBox const&)> Commit)
{
	// Event handlers refer to their sender, so retired text boxes do not own themselves.
	Input.TextChanged(
		[this, Commit = std::move(Commit)](auto const& Sender, auto&&)
		{
			if (CommittingText || Working)
				return;
			auto Input = Sender.template as<Controls::TextBox>();
			if (Input == ArmorThickness && UpdatingArmor)
				return;
			if (!Input.IsReadOnly())
				PendingTextEdits[winrt::get_abi(Input)] = {winrt::make_weak(Input), Commit,
														   std::chrono::steady_clock::now(), false};
		});
	Input.LostFocus(
		[this](auto const& Sender, auto&&)
		{
			if (!Working)
				FlushTextEdits(true, winrt::get_abi(Sender.template as<Controls::TextBox>()));
		});
}
bool EditorWindow::FlushTextEdits(bool Force, void* Only)
{
	if (CommittingText)
		return true;
	auto Now = std::chrono::steady_clock::now();
	std::vector<void*> Ready;
	for (auto const& [Key, Edit] : PendingTextEdits)
		if ((!Only || Key == Only) &&
			(Force || (!Edit.Invalid && Now - Edit.Changed >= std::chrono::seconds(5))))
			Ready.push_back(Key);
	bool Valid = true;
	CommittingText = true;
	for (auto Key : Ready)
	{
		auto Edit = std::move(PendingTextEdits.at(Key));
		PendingTextEdits.erase(Key);
		if (auto Input = Edit.Input.get())
		{
			try
			{
				if (Edit.Commit(Input))
					continue;
			}
			catch (std::exception const& Error)
			{
				SetStatus(HString(Error.what()));
			}
			catch (winrt::hresult_error const& Error)
			{
				SetStatus(Error.message());
			}
			Edit.Invalid = true;
			PendingTextEdits.emplace(Key, std::move(Edit));
			Valid = false;
		}
	}
	CommittingText = false;
	return Valid;
}
void EditorWindow::RequireTextEdits()
{
	if (!FlushTextEdits())
		throw std::runtime_error(String(Tr("Parameters.InvalidInput")));
}
void EditorWindow::SetWorking(bool Value, char const* StatusKey)
{
	Working = Value;
	Busy.IsActive(Value);
	WorkProgress.IsIndeterminate(Value);
	WorkProgress.Visibility(Value ? Xaml::Visibility::Visible : Xaml::Visibility::Collapsed);
	Navigation.IsEnabled(!Value);
	if (StatusKey)
		SetStatus(Tr(StatusKey));
}
} // namespace OffHanger::Ui
