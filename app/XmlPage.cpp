#include "StudioWindow.h"
namespace OffHangar::Ui
{
using namespace winrt;
using namespace Controls;
Grid StudioWindow::BuildXmlPage()
{
    auto Page = Rows({Auto(), Auto(), Star()});
    auto Commands = Stack(true, 8);
    Commands.Children().Append(Button("Xml.Apply", [this] { ApplyXml(); }));
    Commands.Children().Append(Button("Xml.Reset", [this] { ResetXml(); }));
    Commands.Children().Append(Label(Tr("Xml.Workflow"), 12));
    Page.Children().Append(Commands);
    VehicleDocuments.clear();
    DocumentList = Combo({});
    auto const& Vehicle = GameCatalog.Vehicles.at(ActiveVehicle);
    for (auto const& Scope : VehicleData.VehicleScopes(Vehicle))
        if (std::find(VehicleDocuments.begin(), VehicleDocuments.end(), Scope.Path) == VehicleDocuments.end())
        {
            VehicleDocuments.push_back(Scope.Path);
            DocumentList.Items().Append(box_value(HString(Scope.Path)));
        }
    Grid::SetRow(DocumentList, 1);
    Page.Children().Append(DocumentList);
    auto Body = Columns({Star(3), Star(2)});
    Grid::SetRow(Body, 2);
    Page.Children().Append(Body);
    XmlTree = TreeView();
    XmlTree.SelectionMode(TreeViewSelectionMode::Single);
    Body.Children().Append(XmlTree);
    auto Inspector = Stack(false, 10);
    Grid::SetColumn(Inspector, 1);
    Body.Children().Append(Inspector);
    XmlPath = Label(L"", 12);
    XmlPath.IsTextSelectionEnabled(true);
    Inspector.Children().Append(XmlPath);
    XmlValue = TextBox();
    XmlValue.AcceptsReturn(true);
    XmlValue.TextWrapping(Xaml::TextWrapping::Wrap);
    XmlValue.MaxHeight(250);
    XmlValue.IsReadOnly(true);
    Inspector.Children().Append(XmlValue);
    XmlHint = Label(Tr("Xml.ChooseNode"), 12);
    Inspector.Children().Append(XmlHint);
    XmlTree.Expanding(
        [this](TreeView const&, TreeViewExpandingEventArgs const& Event)
        {
            Run(
                [&]
                {
                    auto Parent = Event.Node();
                    auto Address = TreeAddresses.at(get_abi(Parent));
                    if (Parent.HasUnrealizedChildren())
                        ExpandXml(Parent, Address);
                });
        });
    XmlTree.ItemInvoked(
        [this](TreeView const& Sender, TreeViewItemInvokedEventArgs const& Event)
        {
            Run(
                [&]
                {
                    if (auto Node = Event.InvokedItem().try_as<TreeViewNode>())
                        SelectXml(Node);
                    else if (auto Selected = Sender.SelectedNode())
                        SelectXml(Selected);
                });
        });
    XmlValue.TextChanged(
        [this](auto&&, auto&&)
        {
            if (UpdatingXml || !SelectedXmlNode || XmlValue.IsReadOnly() ||
                ActiveVehicle >= GameCatalog.Vehicles.size())
                return;
            auto const& Vehicle = GameCatalog.Vehicles[ActiveVehicle];
            auto& Data = VehicleData.Document(ActiveDocument).Current;
            auto Names = VehicleWorkspace::Names(Data, SelectedXmlAddress);
            auto Key = ActiveDocument + VehicleWorkspace::Display(Names);
            auto Text = String(XmlValue.Text());
            auto const& Node = VehicleWorkspace::Resolve(Data, SelectedXmlAddress);
            if (Text == Node.Value)
                XmlDrafts.erase(Key);
            else
            {
                VehicleValue Field{ActiveDocument,    VehicleWorkspace::Display(Names), Text, "", "",
                                   SelectedXmlAddress};
                if (!XmlDrafts.contains(Key))
                    XmlDrafts[Key] = {Vehicle.Name, Field, Node.Value, Names};
                else
                    XmlDrafts[Key].Field.Value = Text;
            }
            SelectedXmlNode.Content(box_value(HString(Node.Name + " = " + Text.substr(0, 100))));
            XmlHint.Text(Tr("Xml.Pending") + HString(std::to_string(XmlDrafts.size())));
        });
    DocumentList.SelectionChanged(
        [this](auto&&, auto&&)
        {
            Run(
                [&]
                {
                    auto Index = DocumentList.SelectedIndex();
                    if (Index >= 0)
                    {
                        ActiveDocument = VehicleDocuments.at(Index);
                        RefreshXmlTree();
                    }
                });
        });
    auto Previous = std::find(VehicleDocuments.begin(), VehicleDocuments.end(), ActiveDocument);
    if (!VehicleDocuments.empty())
    {
        DocumentList.SelectedIndex(
            Previous == VehicleDocuments.end() ? 0 : int(Previous - VehicleDocuments.begin()));
        // Appending the first ComboBox item can select it before the handler
        // is connected. Initialize explicitly rather than relying on an event.
        ActiveDocument = VehicleDocuments.at(DocumentList.SelectedIndex());
        RefreshXmlTree();
    }
    return Page;
}
TreeViewNode StudioWindow::XmlNode(NodeAddress const& Address)
{
    auto const& Data = VehicleData.Document(ActiveDocument).Current;
    auto const& Node = VehicleWorkspace::Resolve(Data, Address);
    TreeViewNode Item;
    auto Key = ActiveDocument + VehicleWorkspace::Display(VehicleWorkspace::Names(Data, Address));
    auto Found = XmlDrafts.find(Key);
    auto Value = Found == XmlDrafts.end() ? Node.Value : Found->second.Field.Value;
    Item.Content(box_value(HString(Node.Name + (Value.empty() ? "" : " = " + Value.substr(0, 100)))));
    Item.HasUnrealizedChildren(!Node.Children.empty());
    TreeAddresses[get_abi(Item)] = Address;
    return Item;
}
void StudioWindow::ExpandXml(TreeViewNode const& Parent, NodeAddress const& Address)
{
    auto const& Node = VehicleWorkspace::Resolve(VehicleData.Document(ActiveDocument).Current, Address);
    Parent.Children().Clear();
    for (size_t Index = 0; Index < Node.Children.size(); ++Index)
    {
        auto Child = Address;
        Child.push_back(Index);
        Parent.Children().Append(XmlNode(Child));
    }
    Parent.HasUnrealizedChildren(false);
}
void StudioWindow::RefreshXmlTree()
{
    UpdatingXml = true;
    SelectedXmlNode = nullptr;
    XmlValue.Text(L"");
    XmlValue.IsReadOnly(true);
    XmlPath.Text(L"");
    XmlHint.Text(Tr("Xml.ChooseNode"));
    XmlTree.RootNodes().Clear();
    TreeAddresses.clear();
    for (auto const& Scope : VehicleData.VehicleScopes(GameCatalog.Vehicles.at(ActiveVehicle)))
        if (NormalizeResourcePath(Scope.Path) == NormalizeResourcePath(ActiveDocument))
        {
            auto Node = XmlNode(Scope.Root);
            XmlTree.RootNodes().Append(Node);
            ExpandXml(Node, Scope.Root);
            Node.IsExpanded(true);
        }
    UpdatingXml = false;
}
void StudioWindow::SelectXml(TreeViewNode const& Node)
{
    SelectedXmlNode = Node;
    SelectedXmlAddress = TreeAddresses.at(get_abi(Node));
    auto const& Data = VehicleData.Document(ActiveDocument).Current;
    auto const& Value = VehicleWorkspace::Resolve(Data, SelectedXmlAddress);
    auto Key = ActiveDocument + VehicleWorkspace::Display(VehicleWorkspace::Names(Data, SelectedXmlAddress));
    UpdatingXml = true;
    XmlPath.Text(HString(Key));
    XmlValue.IsReadOnly(GameRunning() || VehicleWorkspace::IsReadOnly(Value));
    auto Found = XmlDrafts.find(Key);
    XmlValue.Text(HString(Found == XmlDrafts.end() ? Value.Value : Found->second.Field.Value));
    XmlHint.Text(Tr(XmlValue.IsReadOnly() ? "Xml.ReadOnly" : "Xml.ValueOnly"));
    UpdatingXml = false;
}
void StudioWindow::ApplyXml()
{
    if (ActiveDocument.empty())
        return;
    std::vector<VehicleValue> Changes;
    auto const& Vehicle = GameCatalog.Vehicles.at(ActiveVehicle);
    for (auto const& [Key, Draft] : XmlDrafts)
        if (Draft.Field.Document == ActiveDocument)
        {
            auto Field = Draft.Field;
            Field.Address =
                VehicleWorkspace::Locate(VehicleData.Document(ActiveDocument).Current, Draft.Names);
            if (VehicleWorkspace::Resolve(VehicleData.Document(ActiveDocument).Current, Field.Address)
                    .Value != Draft.Before)
                throw std::runtime_error(String(Tr("Xml.ConcurrentChange")));
            Changes.push_back(std::move(Field));
        }
    VehicleData.EditBatch(Vehicle, Changes);
    std::erase_if(XmlDrafts, [&](auto const& Pair) { return Pair.second.Field.Document == ActiveDocument; });
    RefreshXmlTree();
    RefreshVehicles();
    SetStatus(Tr("Xml.Applied"));
}
fire_and_forget StudioWindow::ResetXml()
{
    auto Lifetime = shared_from_this();
    try
    {
        if (ActiveDocument.empty())
            co_return;
        if (!(co_await Confirm(Tr("Xml.Reset"), Tr("Xml.ResetWarning") + L"\n" + HString(ActiveDocument))))
            co_return;
        VehicleData.ResetDocument(ActiveDocument);
        std::erase_if(XmlDrafts,
                      [&](auto const& Pair) { return Pair.second.Field.Document == ActiveDocument; });
        RefreshXmlTree();
        SetStatus(Tr("Vehicle.Cached"));
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
} // namespace OffHangar::Ui
