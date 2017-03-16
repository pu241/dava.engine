#include "Classes/PropertyPanel/NMaterialExtensions.h"

#include <TArc/Core/OperationInvoker.h>

#include <QtTools/WidgetHelpers/SharedIcon.h>
#include <Render/Material/NMaterial.h>
#include "Application/REGlobal.h"

namespace NMaterialExtensionsDetail
{
using namespace DAVA;
class OpenMaterialEditorProducer : public DAVA::M::CommandProducer
{
public:
    bool IsApplyable(const Reflection& field) const override;
    Info GetInfo() const override;
    bool OnlyForSingleSelection() const override;
    std::unique_ptr<Command> CreateCommand(const Reflection& field, const Params& params) const override;
};

bool OpenMaterialEditorProducer::IsApplyable(const Reflection& field) const
{
    return field.GetValueType() == Type::Instance<NMaterial*>();
}

M::CommandProducer::Info OpenMaterialEditorProducer::GetInfo() const
{
    Info info;
    info.icon = SharedIcon(":/QtIcons/3d.png");
    info.tooltip = "Edit material";
    return info;
}

bool OpenMaterialEditorProducer::OnlyForSingleSelection() const
{
    return true;
}

std::unique_ptr<Command> OpenMaterialEditorProducer::CreateCommand(const Reflection& field, const Params& params) const
{
    NMaterial* material = *field.GetValueObject().GetPtr<NMaterial*>();
    params.invoker->Invoke(REGlobal::ShowMaterial.ID, material);
    return std::unique_ptr<Command>();
}
}

DAVA::M::CommandProducerHolder CreateNMaterialCommandProducer()
{
    using namespace NMaterialExtensionsDetail;
    DAVA::M::CommandProducerHolder holder;
    holder.AddCommandProducer(std::make_unique<OpenMaterialEditorProducer>());
    return holder;
}
