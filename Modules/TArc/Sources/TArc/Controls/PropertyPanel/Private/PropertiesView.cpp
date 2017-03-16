#include "TArc/Controls/PropertyPanel/PropertiesView.h"
#include "TArc/Controls/PropertyPanel/Private/ReflectedPropertyModel.h"
#include "TArc/Controls/PropertyPanel/Private/PropertiesViewDelegate.h"
#include "TArc/Core/ContextAccessor.h"

#include <Reflection/Reflection.h>
#include <Base/BaseTypes.h>

#include <QHBoxLayout>
#include <QTreeView>
#include <QScrollBar>
#include <QHeaderView>
#include <QTimer>
#include <QPainter>

namespace DAVA
{
namespace TArc
{
namespace PropertiesViewDetail
{
const char* SeparatorPositionKey = "SeparatorPosition";

class PropertiesTreeView : public QTreeView
{
public:
    PropertiesTreeView(QWidget* parent)
        : QTreeView(parent)
    {
    }

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const override
    {
        QTreeView::drawRow(painter, options, index);

        QColor gridColor = options.palette.color(QPalette::Normal, QPalette::Window);

        painter->save();
        // draw horizontal bottom line
        painter->setPen(gridColor);
        painter->drawLine(options.rect.bottomLeft(), options.rect.bottomRight());

        // draw vertical line
        if (!(options.state & QStyle::State_Selected))
        {
            QHeaderView* hdr = header();
            if (hdr != nullptr && hdr->count() > 1)
            {
                int sz = hdr->sectionSize(0);
                QScrollBar* scroll = horizontalScrollBar();
                if (scroll != nullptr)
                {
                    sz -= scroll->value();
                }

                QPoint p1 = options.rect.topLeft();
                QPoint p2 = options.rect.bottomLeft();

                p1.setX(p1.x() + sz - 1);
                p2.setX(p2.x() + sz - 1);

                painter->setPen(gridColor);
                painter->drawLine(p1, p2);
            }
        }

        painter->restore();
    }
};

} // namespace PropertiesViewDetail

PropertiesView::PropertiesView(const Params& params_)
    : binder(params_.accessor)
    , params(params_)
{
    binder.BindField(params.objectsField, MakeFunction(this, &PropertiesView::OnObjectsChanged));
    model.reset(new ReflectedPropertyModel(params.accessor, params.invoker, params.ui));

    SetupUI();

    std::shared_ptr<Updater> lockedUpdater = params.updater.lock();
    if (lockedUpdater != nullptr)
    {
        updateConnectionID = lockedUpdater->update.Connect(this, &PropertiesView::Update);
    }

    PropertiesItem viewItem = params.accessor->CreatePropertiesNode(params.settingsNodeName);
    int columnWidth = viewItem.Get(PropertiesViewDetail::SeparatorPositionKey, view->columnWidth(0));
    view->setColumnWidth(0, columnWidth);

    model->LoadExpanded(viewItem.CreateSubHolder("expandedList"));
    QObject::connect(view, &QTreeView::expanded, this, &PropertiesView::OnExpanded);
    QObject::connect(view, &QTreeView::collapsed, this, &PropertiesView::OnCollapsed);
}

PropertiesView::~PropertiesView()
{
    std::shared_ptr<Updater> lockedUpdater = params.updater.lock();
    if (lockedUpdater != nullptr)
    {
        lockedUpdater->update.Disconnect(updateConnectionID);
    }

    PropertiesItem viewSettings = params.accessor->CreatePropertiesNode(params.settingsNodeName);
    viewSettings.Set(PropertiesViewDetail::SeparatorPositionKey, view->columnWidth(0));

    PropertiesItem item = viewSettings.CreateSubHolder("expandedList");
    model->SaveExpanded(item);
}

void PropertiesView::RegisterExtension(const std::shared_ptr<ExtensionChain>& extension)
{
    model->RegisterExtension(extension);
}

void PropertiesView::UnregisterExtension(const std::shared_ptr<ExtensionChain>& extension)
{
    model->UnregisterExtension(extension);
}

void PropertiesView::SetupUI()
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setMargin(0);
    layout->setSpacing(0);
    setLayout(layout);

    view = new PropertiesViewDetail::PropertiesTreeView(this);
    view->setEditTriggers(QAbstractItemView::CurrentChanged | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(view);

    view->setModel(model.get());
    view->setRootIndex(QModelIndex());
    view->setItemDelegate(new PropertiesViewDelegate(view, model.get(), this));

    QHeaderView* headerView = view->header();
    connections.AddConnection(headerView, &QHeaderView::sectionResized, MakeFunction(this, &PropertiesView::OnColumnResized));
}

void PropertiesView::OnObjectsChanged(const Any& objects)
{
    if (objects.IsEmpty())
    {
        model->SetObjects(Vector<Reflection>());
        return;
    }

    DVASSERT(objects.CanCast<Vector<Reflection>>());
    model->SetObjects(objects.Cast<Vector<Reflection>>());
    QModelIndexList expandedLIst = model->GetExpandedList();
    foreach (const QModelIndex& index, expandedLIst)
    {
        view->expand(index);
    }
}

void PropertiesView::OnColumnResized(int columnIndex, int oldSize, int newSize)
{
    PropertiesViewDelegate* d = qobject_cast<PropertiesViewDelegate*>(view->itemDelegate());
    DVASSERT(d != nullptr);
    d->UpdateSizeHints(columnIndex, newSize);
}

void PropertiesView::Update(UpdatePolicy policy)
{
    switch (policy)
    {
    case DAVA::TArc::PropertiesView::FullUpdate:
        model->Update();
        break;
    case DAVA::TArc::PropertiesView::FastUpdate:
        model->UpdateFast();
        break;
    default:
        DVASSERT(false, "Unimplemented update policy have been received");
        break;
    }
}

void PropertiesView::OnExpanded(const QModelIndex& index)
{
    //model->SetExpanded(true, index);
    model->HideEditors();
}

void PropertiesView::OnCollapsed(const QModelIndex& index)
{
    //model->SetExpanded(false, index);
    model->HideEditors();
}

} // namespace TArc
} // namespace DAVA