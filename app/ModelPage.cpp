#include "StudioWindow.h"
#include "ModelCache.h"
#include <sstream>
namespace OffHangar::Ui
{
using namespace winrt;
using namespace Controls;
namespace
{
std::string ThicknessText(float Thickness)
{
    std::ostringstream Text;
    Text << Thickness;
    return Text.str();
}
} // namespace
bool StudioWindow::ArmorVisible() const
{
    return CurrentPage == 0 && CurrentVehicleTab == 2 && ActiveVehicle < GameCatalog.Vehicles.size();
}
void StudioWindow::DeactivateArmor()
{
    ++ArmorGeneration;
    if (ModelView)
        ModelView->SetActive(false);
    ModelView.reset();
    ActiveMesh.reset();
    ArmorFields.clear();
    LoadedVehicle.clear();
    if (ArmorItem)
        ArmorItem.Content(nullptr);
}
Grid StudioWindow::BuildModelPanel()
{
    auto Page = Rows({Auto(), Star()});
    ArmorInfo = Label(Tr("Armor.Loading"), 12);
    Page.Children().Append(ArmorInfo);
    auto Body = Columns({Star(), Pixels(300)});
    Grid::SetRow(Body, 1);
    Page.Children().Append(Body);
    ModelView = ModelViewport::Create();
    Body.Children().Append(ModelView->Element());
    ModelView->OnError = [this](auto const& Error) { SetStatus(HString(Error)); };
    auto Inspector = Rows({Auto(), Star(), Auto(), Auto()});
    Grid::SetColumn(Inspector, 1);
    Body.Children().Append(Inspector);
    Inspector.Children().Append(Label(Tr("Armor.Zones"), 18));
    ArmorMaterials = ListView();
    Grid::SetRow(ArmorMaterials, 1);
    Inspector.Children().Append(ArmorMaterials);
    ArmorThickness = TextBox();
    ArmorThickness.Header(box_value(Tr("Armor.Thickness")));
    ArmorThickness.IsReadOnly(true);
    Grid::SetRow(ArmorThickness, 2);
    Inspector.Children().Append(ArmorThickness);
    Grid::SetRow(Inspector, 0);
    auto Help = Label(Tr("Armor.Controls"), 12);
    Grid::SetRow(Help, 3);
    Inspector.Children().Append(Help);
    auto Select = [this](int Index)
    {
        if (!ActiveMesh || Index < 0 || size_t(Index) >= ActiveMesh->Materials.size())
            return;
        UpdatingArmor = true;
        auto Found = ArmorFields.find(uint32_t(Index));
        ArmorThickness.IsReadOnly(GameRunning() || Found == ArmorFields.end());
        auto Value = Found == ArmorFields.end()
                         ? ThicknessText(ActiveMesh->Materials[Index].Thickness)
                         : VehicleWorkspace::Resolve(VehicleData.Document(Found->second.Document).Current,
                                                     Found->second.Address)
                               .Value;
        ArmorThickness.Text(HString(Value));
        if (Found != ArmorFields.end())
        {
            std::string Description = Found->second.Document + Found->second.DisplayPath;
            for (auto const& Name : Found->second.SharedVehicles)
                Description += "\n" + Name;
            ArmorInfo.Text(HString(Description));
        }
        else
            ArmorInfo.Text(Tr("Armor.Unmapped"));
        if (ModelView)
            ModelView->SelectMaterial(uint32_t(Index));
        UpdatingArmor = false;
    };
    ArmorMaterials.SelectionChanged([Select](auto const& Sender, auto&&)
                                    { Select(Sender.as<ListView>().SelectedIndex()); });
    ModelView->OnPicked = [this, Select](uint32_t Material)
    {
        if (Material == UINT32_MAX)
            return;
        ArmorMaterials.SelectedIndex(int(Material));
        Select(int(Material));
    };
    ArmorThickness.TextChanged(
        [this](auto&&, auto&&)
        {
            if (UpdatingArmor || ArmorThickness.IsReadOnly())
                return;
            try
            {
                UpdateArmorThickness();
            }
            catch (std::exception const& Error)
            {
                SetStatus(HString(Error.what()));
            }
        });
    return Page;
}
fire_and_forget StudioWindow::LoadModel()
{
    try
    {
        auto Lifetime = shared_from_this();
        if (!ArmorVisible() || !ModelView)
            co_return;
        auto Generation = ++ArmorGeneration;
        auto Vehicle = GameCatalog.Vehicles.at(ActiveVehicle);
        auto Definition = VehicleData.Document(Vehicle.DefinitionPath).Current;
        auto Snapshot = std::make_shared<Catalog>(GameCatalog);
        auto CacheFolder = Preferences.CacheFolder / L"Models";
        AssemblySelection Selection;
        Selection.Collision = true;
        apartment_context UiThread;
        std::shared_ptr<Mesh> Loaded;
        std::exception_ptr Failure;
        co_await resume_background();
        try
        {
            auto Cached = LoadModelCache(CacheFolder, Snapshot->Resources.LoadedSignature, Vehicle,
                                         Definition, Selection);
            if (Cached)
                Loaded = std::make_shared<Mesh>(std::move(*Cached));
            else
            {
                Loaded = std::make_shared<Mesh>(BuildVehicleMesh(*Snapshot, Vehicle, Definition, Selection));
                try
                {
                    SaveModelCache(CacheFolder, Snapshot->Resources.LoadedSignature, Vehicle, Definition,
                                   Selection, *Loaded);
                }
                catch (...)
                {
                    if (!GameRunning())
                        throw;
                }
            }
        }
        catch (...)
        {
            Failure = std::current_exception();
        }
        co_await UiThread;
        if (Generation != ArmorGeneration || !ArmorVisible() || !ModelView ||
            GameCatalog.Vehicles[ActiveVehicle].Name != Vehicle.Name)
            co_return;
        if (Failure)
        {
            try
            {
                std::rethrow_exception(Failure);
            }
            catch (std::exception const& Error)
            {
                ArmorInfo.Text(HString(Error.what()));
                Log(Error.what());
            }
            co_return;
        }
        ActiveMesh = std::move(Loaded);
        LoadedVehicle = Vehicle.Name;
        auto Values = VehicleData.Values(Vehicle);
        auto const& Root = Definition.Root;
        auto First = [](Section const* Group)
        { return Group && !Group->Children.empty() ? &Group->Children.front() : nullptr; };
        auto Chassis = First(Root.Child("chassis"));
        auto Turret = First(Root.Child("turrets0"));
        auto Gun = Turret ? First(Turret->Child("guns")) : nullptr;
        for (uint32_t Index = 0; Index < ActiveMesh->Materials.size(); ++Index)
        {
            auto const& Material = ActiveMesh->Materials[Index];
            if (!Material.Zone.starts_with("armor_"))
                continue;
            std::string Prefix, Group, Module;
            if (Material.Component == "Hull")
                Prefix = "/hull";
            if (Material.Component == "Chassis" && Chassis)
            {
                Prefix = "/chassis/" + Chassis->Name;
                Group = "chassis";
                Module = Chassis->Name;
            }
            if (Material.Component == "Turret" && Turret)
            {
                Prefix = "/turrets0/" + Turret->Name;
                Group = "turrets";
                Module = Turret->Name;
            }
            if (Material.Component == "Gun" && Turret && Gun)
            {
                Prefix = "/turrets0/" + Turret->Name + "/guns/" + Gun->Name;
                Group = "guns";
                Module = Gun->Name;
            }
            auto Suffix = "/armor/" + Material.Zone;
            for (auto const& Field : Values)
                if ((Field.Document == Vehicle.DefinitionPath && Field.DisplayPath == Prefix + Suffix) ||
                    (!Group.empty() && Field.Component == Group && Field.Module == Module &&
                     Field.DisplayPath.ends_with(Suffix)))
                {
                    ArmorFields[Index] = Field;
                    break;
                }
        }
        RefreshModelArmor();
        ModelView->SetMesh(ActiveMesh);
        std::string Summary =
            std::to_string(ActiveMesh->Triangles.size()) + " " + String(Tr("Armor.Triangles"));
        for (auto const& Diagnostic : ActiveMesh->Diagnostics)
            Summary += "\n" + Diagnostic;
        ArmorInfo.Text(HString(Summary));
    }
    catch (...)
    {
        co_return;
    }
}
void StudioWindow::RefreshModelArmor()
{
    if (!ActiveMesh || !ArmorMaterials || !ArmorVisible())
        return;
    auto Selection = ArmorMaterials.SelectedIndex();
    UpdatingArmor = true;
    ArmorMaterials.Items().Clear();
    for (uint32_t Index = 0; Index < ActiveMesh->Materials.size(); ++Index)
    {
        auto& Material = ActiveMesh->Materials[Index];
        if (auto Found = ArmorFields.find(Index); Found != ArmorFields.end())
            Material.Thickness =
                std::stof(VehicleWorkspace::Resolve(VehicleData.Document(Found->second.Document).Current,
                                                    Found->second.Address)
                              .Value);
        ArmorMaterials.Items().Append(ListRow(HString(Material.Component + " / " + Material.Zone + " · " +
                                                      ThicknessText(Material.Thickness) + " mm")));
    }
    if (Selection >= 0 && size_t(Selection) < ActiveMesh->Materials.size())
        ArmorMaterials.SelectedIndex(Selection);
    UpdatingArmor = false;
    if (ModelView)
        ModelView->SelectMaterial(Selection >= 0 ? uint32_t(Selection) : UINT32_MAX);
}
void StudioWindow::UpdateArmorThickness()
{
    auto Index = ArmorMaterials.SelectedIndex();
    if (!ArmorVisible() || !ActiveMesh || Index < 0 ||
        LoadedVehicle != GameCatalog.Vehicles[ActiveVehicle].Name)
        return;
    auto Found = ArmorFields.find(uint32_t(Index));
    if (Found == ArmorFields.end())
        return;
    if (VehicleWorkspace::Resolve(VehicleData.Document(Found->second.Document).Current, Found->second.Address)
            .Value == String(ArmorThickness.Text()))
        return;
    VehicleData.Edit(GameCatalog.Vehicles[ActiveVehicle], Found->second.Document, Found->second.Address,
                     String(ArmorThickness.Text()));
    // Refresh the drawing without replacing the active text box or moving its caret.
    ActiveMesh->Materials[Index].Thickness = std::stof(String(ArmorThickness.Text()));
    ArmorMaterials.Items().GetAt(Index).as<ListViewItem>().Content(
        box_value(HString(ActiveMesh->Materials[Index].Component + " / " + ActiveMesh->Materials[Index].Zone +
                          " · " + String(ArmorThickness.Text()) + " mm")));
    ModelView->SelectMaterial(uint32_t(Index));
    SetStatus(Tr("Vehicle.Cached"));
}
} // namespace OffHangar::Ui
