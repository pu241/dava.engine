#include "ChangeStylePropertyCommand.h"

#include "Document/CommandsBase/QECommandIDs.h"
#include "Model/PackageHierarchy/PackageNode.h"
#include "Model/PackageHierarchy/StyleSheetNode.h"
#include "Model/ControlProperties/AbstractProperty.h"

ChangeStylePropertyCommand::ChangeStylePropertyCommand(PackageNode* _root, StyleSheetNode* _node, AbstractProperty* prop, const DAVA::VariantType& newVal)
    : QECommand(CMDID_CHANGE_STYLE_PROPERTY, DAVA::String("change ") + property->GetName().c_str())
    , root(SafeRetain(_root))
    , node(SafeRetain(_node))
    , property(SafeRetain(prop))
    , newValue(newVal)
{
    oldValue = property->GetValue();
}

ChangeStylePropertyCommand::~ChangeStylePropertyCommand()
{
    SafeRelease(root);
    SafeRelease(node);
    SafeRelease(property);
}

void ChangeStylePropertyCommand::Redo()
{
    root->SetStyleProperty(node, property, newValue);
}

void ChangeStylePropertyCommand::Undo()
{
    root->SetStyleProperty(node, property, oldValue);
}
