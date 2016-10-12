#include "../rhi_ShaderSource.h"
#include "../rhi_Public.h"

#include "Base/Hash.h"
//#define PROFILER_ENABLED 1
//#include "Debug/Profiler.h"

    #include "Logger/Logger.h"
using DAVA::Logger;
    #include "FileSystem/DynamicMemoryFile.h"
    #include "FileSystem/FileSystem.h"
using DAVA::DynamicMemoryFile;
    #include "Utils/Utils.h"
    #include "Debug/CPUProfiler.h"
    #include "Concurrency/Mutex.h"
    #include "Concurrency/LockGuard.h"
using DAVA::Mutex;
using DAVA::LockGuard;

    #include "PreProcess.h"
    
    #include "Parser/sl_Parser.h"
    #include "Parser/sl_Tree.h"
    #include "Parser/sl_GeneratorHLSL.h"
    #include "Parser/sl_GeneratorGLES.h"
    #include "Parser/sl_GeneratorMSL.h"

namespace rhi
{
//==============================================================================

ShaderSource::ShaderSource(const char* filename)
    : fileName(filename)
    ,
    ast(nullptr)
{
}

//------------------------------------------------------------------------------

ShaderSource::~ShaderSource()
{
    delete ast;
    ast = nullptr;
}

//------------------------------------------------------------------------------

static rhi::BlendOp
BlendOpFromText(const char* op)
{
    if (stricmp(op, "zero") == 0)
        return rhi::BLENDOP_ZERO;
    else if (stricmp(op, "one") == 0)
        return rhi::BLENDOP_ONE;
    else if (stricmp(op, "src_alpha") == 0)
        return rhi::BLENDOP_SRC_ALPHA;
    else if (stricmp(op, "inv_src_alpha") == 0)
        return rhi::BLENDOP_INV_SRC_ALPHA;
    else if (stricmp(op, "src_color") == 0)
        return rhi::BLENDOP_SRC_COLOR;
    else if (stricmp(op, "dst_color") == 0)
        return rhi::BLENDOP_DST_COLOR;
    else
        return rhi::BLENDOP_ONE;
}

//------------------------------------------------------------------------------

bool ShaderSource::Construct(ProgType progType, const char* srcText)
{
    std::vector<std::string> def;

    return ShaderSource::Construct(progType, srcText, def);
}

//------------------------------------------------------------------------------

bool ShaderSource::Construct(ProgType progType, const char* srcText, const std::vector<std::string>& defines)
{
    bool success = false;
    std::vector<std::string> def;
    const char* argv[128];
    int argc = 0;
    std::string src;

    // pre-process source text with #defines, if any

    DVASSERT(defines.size() % 2 == 0);
    def.reserve(defines.size() / 2);
    for (size_t i = 0, n = defines.size() / 2; i != n; ++i)
    {
        const char* s1 = defines[i * 2 + 0].c_str();
        const char* s2 = defines[i * 2 + 1].c_str();
        def.push_back(DAVA::Format("-D %s=%s", s1, s2));
    }
    for (unsigned i = 0; i != def.size(); ++i)
        argv[argc++] = def[i].c_str();
    SetPreprocessCurFile(fileName.c_str());
    PreProcessText(srcText, argv, argc, &src);

#if 0
{
    Logger::Info("\n\nsrc-code:");

    char ss[64 * 1024];
    unsigned line_cnt = 0;

    if (strlen(src.c_str()) < sizeof(ss))
    {
        strcpy(ss, src.c_str());

        const char* line = ss;
        for (char* s = ss; *s; ++s)
        {
            if( *s=='\r')
                *s=' ';

            if (*s == '\n')
            {
                *s = 0;
                Logger::Info("%4u |  %s", 1 + line_cnt, line);
                line = s+1;
                ++line_cnt;
            }
        }
    }
    else
    {
        Logger::Info(src.c_str());
    }
}
#endif

    static sl::Allocator alloc;
    sl::HLSLParser parser(&alloc, "<shader>", src.c_str(), strlen(src.c_str()));
    ast = new sl::HLSLTree(&alloc);

    if (parser.Parse(ast))
    {
        success = ProcessMetaData(ast);
        type = progType;
    }
    else
    {
        delete ast;
        ast = nullptr;
        sl::Log_Error("Parse error\n");
        DVASSERT(ast);
    }

    return success;
}

//------------------------------------------------------------------------------

bool
ShaderSource::ProcessMetaData(sl::HLSLTree* ast)
{
    struct
    prop_t
    {
        sl::HLSLDeclaration* decl;
        sl::HLSLStatement* prev_statement;
    };

    std::vector<prop_t> prop_decl;
    char btype = 'x';

    if (ast->FindGlobalStruct("vertex_in"))
        btype = 'V';
    else if (ast->FindGlobalStruct("fragment_in"))
        btype = 'F';

    // find properties/samplers
    {
        sl::HLSLStatement* statement = ast->GetRoot()->statement;
        sl::HLSLStatement* pstatement = NULL;
        unsigned sampler_reg = 0;

        while (statement)
        {
            if (statement->nodeType == sl::HLSLNodeType_Declaration)
            {
                sl::HLSLDeclaration* decl = (sl::HLSLDeclaration*)statement;

                if (decl->type.flags & sl::HLSLTypeFlag_Property)
                {
                    property.resize(property.size() + 1);
                    rhi::ShaderProp& prop = property.back();

                    prop.uid = DAVA::FastName(decl->name);
                    prop.source = rhi::ShaderProp::SOURCE_AUTO;
                    prop.precision = rhi::ShaderProp::PRECISION_NORMAL;
                    prop.arraySize = 1;
                    prop.isBigArray = false;

                    if (decl->type.array)
                    {
                        if (decl->type.arraySize->nodeType == sl::HLSLNodeType_LiteralExpression)
                        {
                            sl::HLSLLiteralExpression* expr = (sl::HLSLLiteralExpression*)(decl->type.arraySize);

                            if (expr->type == sl::HLSLBaseType_Int)
                                prop.arraySize = expr->iValue;
                        }
                    }

                    if (decl->annotation)
                    {
                        if (strstr(decl->annotation, "bigarray"))
                        {
                            prop.isBigArray = true;
                        }
                    }

                    switch (decl->type.baseType)
                    {
                    case sl::HLSLBaseType_Float:
                        prop.type = rhi::ShaderProp::TYPE_FLOAT1;
                        break;
                    case sl::HLSLBaseType_Float2:
                        prop.type = rhi::ShaderProp::TYPE_FLOAT2;
                        break;
                    case sl::HLSLBaseType_Float3:
                        prop.type = rhi::ShaderProp::TYPE_FLOAT3;
                        break;
                    case sl::HLSLBaseType_Float4:
                        prop.type = rhi::ShaderProp::TYPE_FLOAT4;
                        break;
                    case sl::HLSLBaseType_Float4x4:
                        prop.type = rhi::ShaderProp::TYPE_FLOAT4X4;
                        break;
                    }

                    for (sl::HLSLAttribute* a = decl->attributes; a; a = a->nextAttribute)
                    {
                        if (stricmp(a->attrText, "material") == 0)
                            prop.source = rhi::ShaderProp::SOURCE_MATERIAL;
                        else if (stricmp(a->attrText, "auto") == 0)
                            prop.source = rhi::ShaderProp::SOURCE_AUTO;
                        else
                            prop.tag = FastName(a->attrText);
                    }

                    if (decl->assignment)
                    {
                        if (decl->assignment->nodeType == sl::HLSLNodeType_ConstructorExpression)
                        {
                            sl::HLSLConstructorExpression* ctor = (sl::HLSLConstructorExpression*)(decl->assignment);
                            unsigned val_i = 0;

                            for (sl::HLSLExpression *arg = ctor->argument; arg; arg = arg->nextExpression, ++val_i)
                            {
                                if (arg->nodeType == sl::HLSLNodeType_LiteralExpression)
                                {
                                    sl::HLSLLiteralExpression* expr = (sl::HLSLLiteralExpression*)(arg);

                                    if (expr->type == sl::HLSLBaseType_Float)
                                        prop.defaultValue[val_i] = expr->fValue;
                                    else if (expr->type == sl::HLSLBaseType_Int)
                                        prop.defaultValue[val_i] = float(expr->iValue);
                                }
                            }
                        }
                    }

                    buf_t* cbuf = nullptr;

                    for (std::vector<buf_t>::iterator b = buf.begin(), b_end = buf.end(); b != b_end; ++b)
                    {
                        if (b->source == prop.source && b->tag == prop.tag)
                        {
                            cbuf = &(buf[b - buf.begin()]);
                            break;
                        }
                    }

                    if (!cbuf)
                    {
                        buf.resize(buf.size() + 1);

                        cbuf = &(buf.back());

                        cbuf->source = prop.source;
                        cbuf->tag = prop.tag;
                        cbuf->regCount = 0;
                        cbuf->isArray = false;
                    }

                    prop.bufferindex = static_cast<uint32>(cbuf - &(buf[0]));

                    if (prop.type == rhi::ShaderProp::TYPE_FLOAT1 || prop.type == rhi::ShaderProp::TYPE_FLOAT2 || prop.type == rhi::ShaderProp::TYPE_FLOAT3)
                    {
                        bool do_add = true;
                        uint32 sz = 0;

                        switch (prop.type)
                        {
                        case rhi::ShaderProp::TYPE_FLOAT1:
                            sz = 1;
                            break;
                        case rhi::ShaderProp::TYPE_FLOAT2:
                            sz = 2;
                            break;
                        case rhi::ShaderProp::TYPE_FLOAT3:
                            sz = 3;
                            break;
                        default:
                            break;
                        }

                        for (unsigned r = 0; r != cbuf->avlRegIndex.size(); ++r)
                        {
                            if (cbuf->avlRegIndex[r] + sz <= 4)
                            {
                                prop.bufferReg = r;
                                prop.bufferRegCount = cbuf->avlRegIndex[r];

                                cbuf->avlRegIndex[r] += sz;

                                do_add = false;
                                break;
                            }
                        }

                        if (do_add)
                        {
                            prop.bufferReg = cbuf->regCount;
                            prop.bufferRegCount = 0;

                            ++cbuf->regCount;

                            cbuf->avlRegIndex.push_back(sz);
                        }
                    }
                    else if (prop.type == rhi::ShaderProp::TYPE_FLOAT4 || prop.type == rhi::ShaderProp::TYPE_FLOAT4X4)
                    {
                        prop.bufferReg = cbuf->regCount;
                        prop.bufferRegCount = prop.arraySize * ((prop.type == rhi::ShaderProp::TYPE_FLOAT4) ? 1 : 4);

                        cbuf->regCount += prop.bufferRegCount;

                        if (prop.arraySize > 1)
                        {
                            cbuf->isArray = true;
                            //-                            prop.isBigArray = true;
                        }
                        else
                        {
                            for (int i = 0; i != prop.bufferRegCount; ++i)
                                cbuf->avlRegIndex.push_back(4);
                        }
                    }

                    prop_t pp;

                    pp.decl = decl;
                    pp.prev_statement = pstatement;

                    prop_decl.push_back(pp);
                }

                if (decl->type.baseType == sl::HLSLBaseType_Sampler2D
                    || decl->type.baseType == sl::HLSLBaseType_SamplerCube
                    )
                {
                    sampler.resize(sampler.size() + 1);
                    rhi::ShaderSampler& s = sampler.back();

                    char regName[128];

                    switch (decl->type.baseType)
                    {
                    case sl::HLSLBaseType_Sampler2D:
                        s.type = rhi::TEXTURE_TYPE_2D;
                        break;
                    case sl::HLSLBaseType_SamplerCube:
                        s.type = rhi::TEXTURE_TYPE_CUBE;
                        break;
                    }
                    s.uid = FastName(decl->name);
                    Snprintf(regName, sizeof(regName), "s%u", sampler_reg);
                    ++sampler_reg;
                    decl->registerName = ast->AddString(regName);
                }
            }

            pstatement = statement;
            statement = statement->nextStatement;
        }
    }

    // rename vertex-input variables to pre-defined names
    {
        sl::HLSLStruct* vinput = ast->FindGlobalStruct("vertex_in");
        if (vinput)
        {
            const char* vertex_in = ast->AddString("vertex_in");

            class Replacer : public sl::HLSLTreeVisitor
            {
            public:
                sl::HLSLStructField* field;
                const char* new_name;
                const char* vertex_in;
                virtual void VisitMemberAccess(sl::HLSLMemberAccess* node)
                {
                    if (node->field == field->name
                        && node->object->expressionType.baseType == sl::HLSLBaseType_UserDefined
                        && node->object->expressionType.typeName == vertex_in
                        )
                    {
                        node->field = new_name;
                    }

                    sl::HLSLTreeVisitor::VisitMemberAccess(node);
                }
                void Replace(sl::HLSLTree* ast, sl::HLSLStructField* field_, const char* new_name_)
                {
                    field = field_;
                    new_name = new_name_;
                    VisitRoot(ast->GetRoot());

                    field->name = new_name_;
                }
            };

            Replacer r;

            r.vertex_in = vertex_in;

            struct
            {
                const char* semantic;
                const char* attr_name;
            } attr[] =
            {
              { "POSITION", "position" },
              { "NORMAL", "normal" },
              { "TEXCOORD", "texcoord0" },
              { "TEXCOORD0", "texcoord0" },
              { "TEXCOORD1", "texcoord1" },
              { "TEXCOORD2", "texcoord2" },
              { "TEXCOORD3", "texcoord3" },
              { "TEXCOORD4", "texcoord4" },
              { "TEXCOORD5", "texcoord5" },
              { "TEXCOORD6", "texcoord6" },
              { "TEXCOORD7", "texcoord7" },
              { "COLOR", "color0" },
              { "COLOR0", "color0" },
              { "COLOR1", "color1" },
              { "TANGENT", "tangent" },
              { "BINORMAL", "binormal" },
              { "BLENDWEIGHT", "blendweight" },
              { "BLENDINDICES", "blendindex" }
            };

            for (sl::HLSLStructField* field = vinput->field; field; field = field->nextField)
            {
                if (field->semantic)
                {
                    for (unsigned a = 0; a != countof(attr); ++a)
                    {
                        if (stricmp(field->semantic, attr[a].semantic) == 0)
                        {
                            r.Replace(ast, field, ast->AddString(attr[a].attr_name));
                            break;
                        }
                    }
                }
            }
        }
    }

    // get vertex-layout
    {
        sl::HLSLStruct* input = ast->FindGlobalStruct("vertex_in");

        if (input)
        {
            struct
            {
                rhi::VertexSemantics usage;
                const char* semantic;
            }
            semantic[] =
            {
              { rhi::VS_POSITION, "POSITION" },
              { rhi::VS_NORMAL, "NORMAL" },
              { rhi::VS_COLOR, "COLOR" },
              { rhi::VS_TEXCOORD, "TEXCOORD" },
              { rhi::VS_TANGENT, "TANGENT" },
              { rhi::VS_BINORMAL, "BINORMAL" },
              { rhi::VS_BLENDWEIGHT, "BLENDWEIGHT" },
              { rhi::VS_BLENDINDEX, "BLENDINDICES" }
            };

            vertexLayout.Clear();

            rhi::VertexDataFrequency cur_freq = rhi::VDF_PER_VERTEX;

            for (sl::HLSLStructField* field = input->field; field; field = field->nextField)
            {
                rhi::VertexSemantics usage;
                unsigned usage_i = 0;
                rhi::VertexDataType data_type = rhi::VDT_FLOAT;
                unsigned data_count = 0;
                rhi::VertexDataFrequency freq = rhi::VDF_PER_VERTEX;

                switch (field->type.baseType)
                {
                case sl::HLSLBaseType_Float4:
                    data_type = rhi::VDT_FLOAT;
                    data_count = 4;
                    break;
                case sl::HLSLBaseType_Float3:
                    data_type = rhi::VDT_FLOAT;
                    data_count = 3;
                    break;
                case sl::HLSLBaseType_Float2:
                    data_type = rhi::VDT_FLOAT;
                    data_count = 2;
                    break;
                case sl::HLSLBaseType_Float:
                    data_type = rhi::VDT_FLOAT;
                    data_count = 1;
                    break;
                case sl::HLSLBaseType_Uint4:
                    data_type = rhi::VDT_UINT8;
                    data_count = 4;
                    break;
                }

                char sem[128];

                strcpy(sem, field->semantic);
                for (char* s = sem; *s; ++s)
                    *s = toupper(*s);

                for (unsigned i = 0; i != countof(semantic); ++i)
                {
                    const char* t = strstr(sem, semantic[i].semantic);

                    if (t == sem)
                    {
                        const char* tu = sem + strlen(semantic[i].semantic);

                        usage = semantic[i].usage;
                        usage_i = atoi(tu);

                        break;
                    }
                }

                if (usage == rhi::VS_COLOR)
                {
                    data_type = rhi::VDT_UINT8N;
                    data_count = 4;
                }

                if (field->attribute)
                {
                    if (stricmp(field->attribute->attrText, "vertex") == 0)
                        freq = rhi::VDF_PER_VERTEX;
                    else if (stricmp(field->attribute->attrText, "instance") == 0)
                        freq = rhi::VDF_PER_INSTANCE;
                }

                if (freq != cur_freq)
                    vertexLayout.AddStream(freq);
                cur_freq = freq;

                vertexLayout.AddElement(usage, usage_i, data_type, data_count);
            }
        }
    }

    if (prop_decl.size())
    {
        std::vector<sl::HLSLBuffer*> cbuf_decl;

        cbuf_decl.resize(buf.size());
        for (unsigned i = 0; i != buf.size(); ++i)
        {
            sl::HLSLBuffer* cbuf = ast->AddNode<sl::HLSLBuffer>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
            sl::HLSLDeclaration* decl = ast->AddNode<sl::HLSLDeclaration>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
            sl::HLSLLiteralExpression* sz = ast->AddNode<sl::HLSLLiteralExpression>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
            ;
            char buf_name[128];
            char buf_type_name[128];
            char buf_reg_name[128];

            Snprintf(buf_name, sizeof(buf_name), "%cP_Buffer%u", btype, i);
            Snprintf(buf_type_name, sizeof(buf_name), "%cP_Buffer%u_t", btype, i);
            Snprintf(buf_reg_name, sizeof(buf_name), "b%u", i);

            decl->name = ast->AddString(buf_name);
            decl->type.baseType = sl::HLSLBaseType_Float4;
            decl->type.array = true;
            decl->type.arraySize = sz;

            if (buf[i].isArray)
            {
                unsigned propCount = 0;

                for (unsigned p = 0; p != property.size(); ++p)
                {
                    if (property[p].bufferindex == i)
                        ++propCount;
                }
                if (propCount != 1)
                {
                    Logger::Error("cbuffer with array-property has more than one property :");
                    Logger::Error("  cbuf[%u]", i);
                    for (unsigned p = 0; p != property.size(); ++p)
                    {
                        if (property[p].bufferindex == i)
                            Logger::Error("    %s", property[p].uid.c_str());
                    }
                    return false;
                }

                for (unsigned p = 0; p != property.size(); ++p)
                {
                    if (property[p].bufferindex == i)
                    {
                        decl->name = ast->AddString(property[p].uid.c_str());
                        break;
                    }
                }

                decl->annotation = ast->AddString("bigarray");
                decl->registerName = ast->AddString(buf_name);
            }

            sz->type = sl::HLSLBaseType_Int;
            sz->iValue = buf[i].regCount;

            cbuf->field = decl;
            cbuf->name = ast->AddString(buf_type_name);
            cbuf->registerName = ast->AddString(buf_reg_name);
            cbuf->registerCount = buf[i].regCount;

            cbuf_decl[i] = cbuf;
        }

        for (unsigned i = 0; i != cbuf_decl.size() - 1; ++i)
            cbuf_decl[i]->nextStatement = cbuf_decl[i + 1];

        prop_decl[0].prev_statement->nextStatement = cbuf_decl[0];
        cbuf_decl[cbuf_decl.size() - 1]->nextStatement = prop_decl[0].decl;

        #define DO_FLOAT4_CAST 1

        DVASSERT(property.size() == prop_decl.size());
        for (unsigned i = 0; i != property.size(); ++i)
        {
            switch (property[i].type)
            {
            case rhi::ShaderProp::TYPE_FLOAT4:
            {
                if (!property[i].isBigArray)
                {
                    sl::HLSLArrayAccess* arr_access = ast->AddNode<sl::HLSLArrayAccess>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);
                    sl::HLSLLiteralExpression* idx = ast->AddNode<sl::HLSLLiteralExpression>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
                    sl::HLSLIdentifierExpression* arr = ast->AddNode<sl::HLSLIdentifierExpression>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
                    char buf_name[128];

                    Snprintf(buf_name, sizeof(buf_name), "%cP_Buffer%u", btype, property[i].bufferindex);
                    arr->name = ast->AddString(buf_name);
                    arr->global = true;

                    idx->type = sl::HLSLBaseType_Int;
                    idx->iValue = property[i].bufferReg;

                    arr_access->array = arr;
                    arr_access->index = idx;
                    
                    #if DO_FLOAT4_CAST
                    sl::HLSLCastingExpression* cast_expr = ast->AddNode<sl::HLSLCastingExpression>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);
                    cast_expr->expression = arr_access;
                    cast_expr->type.baseType = sl::HLSLBaseType_Float4;

                    prop_decl[i].decl->assignment = cast_expr;
                    prop_decl[i].decl->type.flags |= sl::HLSLTypeFlag_Static | sl::HLSLTypeFlag_Property;
                    #else
                    prop_decl[i].decl->assignment = arr_access;
                    prop_decl[i].decl->type.flags |= sl::HLSLTypeFlag_Static | sl::HLSLTypeFlag_Property;
                    #endif
                }
                else
                {
                    char buf_name[128];

                    Snprintf(buf_name, sizeof(buf_name), "%cP_Buffer%u", btype, property[i].bufferindex);
                    prop_decl[i].decl->registerName = ast->AddString(buf_name);
                }
            }
            break;

            case rhi::ShaderProp::TYPE_FLOAT3:
            case rhi::ShaderProp::TYPE_FLOAT2:
            case rhi::ShaderProp::TYPE_FLOAT1:
            {
                sl::HLSLMemberAccess* member_access = ast->AddNode<sl::HLSLMemberAccess>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);
                char xyzw[] = { 'x', 'y', 'z', 'w', '\0' };
                unsigned elem_cnt = 0;
                sl::HLSLArrayAccess* arr_access = ast->AddNode<sl::HLSLArrayAccess>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);
                sl::HLSLLiteralExpression* idx = ast->AddNode<sl::HLSLLiteralExpression>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
                sl::HLSLIdentifierExpression* arr = ast->AddNode<sl::HLSLIdentifierExpression>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
                char buf_name[128];

                switch (property[i].type)
                {
                case rhi::ShaderProp::TYPE_FLOAT1:
                    elem_cnt = 1;
                    break;
                case rhi::ShaderProp::TYPE_FLOAT2:
                    elem_cnt = 2;
                    break;
                case rhi::ShaderProp::TYPE_FLOAT3:
                    elem_cnt = 3;
                    break;
                }

                member_access->object = arr_access;
                xyzw[property[i].bufferRegCount + elem_cnt] = 0;
                member_access->field = ast->AddString(xyzw + property[i].bufferRegCount);

                Snprintf(buf_name, sizeof(buf_name), "%cP_Buffer%u", btype, property[i].bufferindex);
                arr->name = ast->AddString(buf_name);
                arr->global = true;

                idx->type = sl::HLSLBaseType_Int;
                idx->iValue = property[i].bufferReg;

                arr_access->array = arr;
                arr_access->index = idx;

                #if DO_FLOAT4_CAST
                sl::HLSLCastingExpression* cast_expr = ast->AddNode<sl::HLSLCastingExpression>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);
                cast_expr->expression = arr_access;
                cast_expr->type.baseType = sl::HLSLBaseType_Float4;
                member_access->object = cast_expr;

                prop_decl[i].decl->assignment = member_access;
                prop_decl[i].decl->type.flags |= sl::HLSLTypeFlag_Static | sl::HLSLTypeFlag_Property;
                #else
                prop_decl[i].decl->assignment = member_access;
                prop_decl[i].decl->type.flags |= sl::HLSLTypeFlag_Static | sl::HLSLTypeFlag_Property;
                #endif
            }
            break;

            case rhi::ShaderProp::TYPE_FLOAT4X4:
            {
                sl::HLSLConstructorExpression* ctor = ast->AddNode<sl::HLSLConstructorExpression>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);
                sl::HLSLArrayAccess* arr_access[4];
                sl::HLSLCastingExpression* cast_expr[4];

                ctor->type.baseType = sl::HLSLBaseType_Float4x4;

                for (unsigned k = 0; k != 4; ++k)
                {
                    arr_access[k] = ast->AddNode<sl::HLSLArrayAccess>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);

                    sl::HLSLLiteralExpression* idx = ast->AddNode<sl::HLSLLiteralExpression>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
                    sl::HLSLIdentifierExpression* arr = ast->AddNode<sl::HLSLIdentifierExpression>(prop_decl[0].decl->fileName, prop_decl[0].decl->line);
                    char buf_name[128];

                    Snprintf(buf_name, sizeof(buf_name), "%cP_Buffer%u", btype, property[i].bufferindex);
                    arr->name = ast->AddString(buf_name);

                    idx->type = sl::HLSLBaseType_Int;
                    idx->iValue = property[i].bufferReg + k;

                    arr_access[k]->array = arr;
                    arr_access[k]->index = idx;
                }

                #if DO_FLOAT4_CAST
                for (unsigned k = 0; k != 4; ++k)
                {
                    cast_expr[k] = ast->AddNode<sl::HLSLCastingExpression>(prop_decl[i].decl->fileName, prop_decl[i].decl->line);
                    cast_expr[k]->expression = arr_access[k];
                    cast_expr[k]->type.baseType = sl::HLSLBaseType_Float4;
                }

                ctor->argument = cast_expr[0];
                for (unsigned k = 0; k != 4 - 1; ++k)
                    cast_expr[k]->nextExpression = cast_expr[k + 1];
                #else
                ctor->argument = arr_access[0];
                for (unsigned k = 0; k != 4 - 1; ++k)
                    arr_access[k]->nextExpression = arr_access[k + 1];
                #endif

                prop_decl[i].decl->assignment = ctor;
                prop_decl[i].decl->type.flags |= sl::HLSLTypeFlag_Static | sl::HLSLTypeFlag_Property;
            }
            break;
            }

            if (property[i].isBigArray)
            {
                prop_decl[i].decl->hidden = true;
            }
        }
    }

    // get blending

    sl::HLSLBlend* blend = ast->GetRoot()->blend;

    if (blend)
    {
        blending.rtBlend[0].blendEnabled = !(blend->src_op == sl::BLENDOP_ONE && blend->dst_op == sl::BLENDOP_ZERO);

        switch (blend->src_op)
        {
        case sl::BLENDOP_ZERO:
            blending.rtBlend[0].colorSrc = rhi::BLENDOP_ZERO;
            break;
        case sl::BLENDOP_ONE:
            blending.rtBlend[0].colorSrc = rhi::BLENDOP_ONE;
            break;
        case sl::BLENDOP_SRC_ALPHA:
            blending.rtBlend[0].colorSrc = rhi::BLENDOP_SRC_ALPHA;
            break;
        case sl::BLENDOP_INV_SRC_ALPHA:
            blending.rtBlend[0].colorSrc = rhi::BLENDOP_INV_SRC_ALPHA;
            break;
        case sl::BLENDOP_SRC_COLOR:
            blending.rtBlend[0].colorSrc = rhi::BLENDOP_SRC_COLOR;
            break;
        case sl::BLENDOP_DST_COLOR:
            blending.rtBlend[0].colorSrc = rhi::BLENDOP_DST_COLOR;
            break;
        }
        switch (blend->dst_op)
        {
        case sl::BLENDOP_ZERO:
            blending.rtBlend[0].colorDst = rhi::BLENDOP_ZERO;
            break;
        case sl::BLENDOP_ONE:
            blending.rtBlend[0].colorDst = rhi::BLENDOP_ONE;
            break;
        case sl::BLENDOP_SRC_ALPHA:
            blending.rtBlend[0].colorDst = rhi::BLENDOP_SRC_ALPHA;
            break;
        case sl::BLENDOP_INV_SRC_ALPHA:
            blending.rtBlend[0].colorDst = rhi::BLENDOP_INV_SRC_ALPHA;
            break;
        case sl::BLENDOP_SRC_COLOR:
            blending.rtBlend[0].colorDst = rhi::BLENDOP_SRC_COLOR;
            break;
        case sl::BLENDOP_DST_COLOR:
            blending.rtBlend[0].colorDst = rhi::BLENDOP_DST_COLOR;
            break;
        }
    }

    // get color write-mask

    sl::HLSLColorMask* mask = ast->GetRoot()->color_mask;

    if (mask)
    {
        switch (mask->mask)
        {
        case sl::COLORMASK_NONE:
            blending.rtBlend[0].writeMask = rhi::COLORMASK_NONE;
            break;
        case sl::COLORMASK_ALL:
            blending.rtBlend[0].writeMask = rhi::COLORMASK_ALL;
            break;
        case sl::COLORMASK_RGB:
            blending.rtBlend[0].writeMask = rhi::COLORMASK_R | rhi::COLORMASK_G | rhi::COLORMASK_B;
            break;
        case sl::COLORMASK_A:
            blending.rtBlend[0].writeMask = rhi::COLORMASK_A;
            break;
        }
    }

#if 0
    Logger::Info("properties (%u) :", property.size());
    for (std::vector<rhi::ShaderProp>::const_iterator p = property.begin(), p_end = property.end(); p != p_end; ++p)
    {
        if (p->type == rhi::ShaderProp::TYPE_FLOAT4 || p->type == rhi::ShaderProp::TYPE_FLOAT4X4)
        {
            if (p->arraySize == 1)
            {
                Logger::Info("  %-16s    buf#%u  -  %u, %u x float4", p->uid.c_str(), p->bufferindex, p->bufferReg, p->bufferRegCount);
            }
            else
            {
                char name[128];

                Snprintf(name, sizeof(name) - 1, "%s[%u]", p->uid.c_str(), p->arraySize);
                Logger::Info("  %-16s    buf#%u  -  %u, %u x float4", name, p->bufferindex, p->bufferReg, p->bufferRegCount);
            }
        }
        else
        {
            const char* xyzw = "xyzw";

            switch (p->type)
            {
            case rhi::ShaderProp::TYPE_FLOAT1:
                Logger::Info("  %-16s    buf#%u  -  %u, %c", p->uid.c_str(), p->bufferindex, p->bufferReg, xyzw[p->bufferRegCount]);
                break;

            case rhi::ShaderProp::TYPE_FLOAT2:
                Logger::Info("  %-16s    buf#%u  -  %u, %c%c", p->uid.c_str(), p->bufferindex, p->bufferReg, xyzw[p->bufferRegCount + 0], xyzw[p->bufferRegCount + 1]);
                break;

            case rhi::ShaderProp::TYPE_FLOAT3:
                Logger::Info("  %-16s    buf#%u  -  %u, %c%c%c", p->uid.c_str(), p->bufferindex, p->bufferReg, xyzw[p->bufferRegCount + 0], xyzw[p->bufferRegCount + 1], xyzw[p->bufferRegCount + 2]);
                break;

            default:
                break;
            }
        }
    }

    Logger::Info("\n--- ShaderSource");
    Logger::Info("buffers (%u) :", buf.size());
    for (unsigned i = 0; i != buf.size(); ++i)
    {
        Logger::Info("  buf#%u  reg.count = %u", i, buf[i].regCount);
    }

    Logger::Info("samplers (%u) :", sampler.size());
    for (unsigned s = 0; s != sampler.size(); ++s)
    {
        Logger::Info("  sampler#%u  \"%s\"", s, sampler[s].uid.c_str());
    }
    Logger::Info("\n\n");
#endif

    return true;
};

//------------------------------------------------------------------------------

static inline bool
ReadUI1(DAVA::File* f, uint8* x)
{
    return (f->Read(x) == sizeof(uint8));
}

static inline bool
ReadUI4(DAVA::File* f, uint32* x)
{
    return (f->Read(x) == sizeof(uint32));
}

static inline bool
ReadS0(DAVA::File* f, std::string* str)
{
    char s0[128 * 1024];
    uint32 sz = 0;
    if (ReadUI4(f, &sz))
    {
        if (f->Read(s0, sz) == sz)
        {
            *str = s0;
            return true;
        }
    }
    return false;
}

bool ShaderSource::Load(Api api, DAVA::File* in)
{
#define READ_CHECK(exp) if (!(exp)) { return false; }

    std::string s0;
    uint32 readUI4;
    uint8 readUI1;

    Reset();

    READ_CHECK(ReadUI4(in, &readUI4));
    type = ProgType(readUI4);

    READ_CHECK(ReadS0(in, code + api));

    READ_CHECK(vertexLayout.Load(in));

    READ_CHECK(ReadUI4(in, &readUI4));
    property.resize(readUI4);
    for (unsigned p = 0; p != property.size(); ++p)
    {
        READ_CHECK(ReadS0(in, &s0));
        property[p].uid = FastName(s0.c_str());

        READ_CHECK(ReadS0(in, &s0));
        property[p].tag = FastName(s0.c_str());

        READ_CHECK(ReadUI4(in, &readUI4));
        property[p].type = ShaderProp::Type(readUI4);

        READ_CHECK(ReadUI4(in, &readUI4));
        property[p].source = ShaderProp::Source(readUI4);

        READ_CHECK(ReadUI4(in, &readUI4));
        property[p].isBigArray = readUI4;

        READ_CHECK(ReadUI4(in, &property[p].arraySize));
        READ_CHECK(ReadUI4(in, &property[p].bufferindex));
        READ_CHECK(ReadUI4(in, &property[p].bufferReg));
        READ_CHECK(ReadUI4(in, &property[p].bufferRegCount));

        READ_CHECK(in->Read(property[p].defaultValue, 16 * sizeof(float)) == 16 * sizeof(float));
    }

    READ_CHECK(ReadUI4(in, &readUI4));
    buf.resize(readUI4);
    for (unsigned b = 0; b != buf.size(); ++b)
    {
        READ_CHECK(ReadUI4(in, &readUI4));
        buf[b].source = ShaderProp::Source(readUI4);

        READ_CHECK(ReadS0(in, &s0));
        buf[b].tag = FastName(s0.c_str());

        READ_CHECK(ReadUI4(in, &buf[b].regCount));
    }

    READ_CHECK(ReadUI4(in, &readUI4));
    sampler.resize(readUI4);
    for (unsigned s = 0; s != sampler.size(); ++s)
    {
        READ_CHECK(ReadUI4(in, &readUI4));
        sampler[s].type = TextureType(readUI4);

        READ_CHECK(ReadS0(in, &s0));
        sampler[s].uid = FastName(s0.c_str());
    }

    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].colorFunc = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].colorSrc = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].colorDst = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].alphaFunc = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].alphaSrc = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].alphaDst = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].writeMask = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].blendEnabled = readUI1;
    READ_CHECK(ReadUI1(in, &readUI1));
    blending.rtBlend[0].alphaToCoverage = readUI1;
    READ_CHECK(in->Seek(3, DAVA::File::SEEK_FROM_CURRENT));
    
#undef READ_CHECK

    return true;
}

//------------------------------------------------------------------------------

static inline bool
WriteUI1(DAVA::File* f, uint8 x)
{
    return (f->Write(&x) == sizeof(x));
}

static inline bool
WriteUI4(DAVA::File* f, uint32 x)
{
    return (f->Write(&x) == sizeof(x));
}

static inline bool
WriteS0(DAVA::File* f, const char* str)
{
    char s0[128 * 1024];
    uint32 sz = L_ALIGNED_SIZE(strlen(str) + 1, sizeof(uint32));

    memset(s0, 0x00, sz);
    strcpy(s0, str);

    if (WriteUI4(f, sz))
    {
        return (f->Write(s0, sz) == sz);
    }
    return false;
}

bool ShaderSource::Save(Api api, DAVA::File* out) const
{
#define WRITE_CHECK(exp) if (!(exp)) { return false; }

    if (code[api].length() == 0)
    {
        DVASSERT(ast);
        if (ast)
            GetSourceCode(api);
        else
            return false;
    }

    WRITE_CHECK(WriteUI4(out, type));
    WRITE_CHECK(WriteS0(out, code[api].c_str()));

    WRITE_CHECK(vertexLayout.Save(out));

    WRITE_CHECK(WriteUI4(out, static_cast<uint32>(property.size())));
    for (unsigned p = 0; p != property.size(); ++p)
    {
        WRITE_CHECK(WriteS0(out, property[p].uid.c_str()));
        WRITE_CHECK(WriteS0(out, property[p].tag.c_str()));
        WRITE_CHECK(WriteUI4(out, property[p].type));
        WRITE_CHECK(WriteUI4(out, property[p].source));
        WRITE_CHECK(WriteUI4(out, property[p].isBigArray));
        WRITE_CHECK(WriteUI4(out, property[p].arraySize));
        WRITE_CHECK(WriteUI4(out, property[p].bufferindex));
        WRITE_CHECK(WriteUI4(out, property[p].bufferReg));
        WRITE_CHECK(WriteUI4(out, property[p].bufferRegCount));
        WRITE_CHECK(out->Write(property[p].defaultValue, 16 * sizeof(float)) == 16 * sizeof(float));
    }

    WRITE_CHECK(WriteUI4(out, static_cast<uint32>(buf.size())));
    for (unsigned b = 0; b != buf.size(); ++b)
    {
        WRITE_CHECK(WriteUI4(out, buf[b].source));
        WRITE_CHECK(WriteS0(out, buf[b].tag.c_str()));
        WRITE_CHECK(WriteUI4(out, buf[b].regCount));
    }

    WRITE_CHECK(WriteUI4(out, static_cast<uint32>(sampler.size())));
    for (unsigned s = 0; s != sampler.size(); ++s)
    {
        WRITE_CHECK(WriteUI4(out, sampler[s].type));
        WRITE_CHECK(WriteS0(out, sampler[s].uid.c_str()));
    }

    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].colorFunc));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].colorSrc));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].colorDst));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].alphaFunc));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].alphaSrc));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].alphaDst));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].writeMask));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].blendEnabled));
    WRITE_CHECK(WriteUI1(out, blending.rtBlend[0].alphaToCoverage));
    WRITE_CHECK(WriteUI1(out, 0));
    WRITE_CHECK(WriteUI1(out, 0));
    WRITE_CHECK(WriteUI1(out, 0));

#undef WRITE_CHECK

    return true;
}

//------------------------------------------------------------------------------

const std::string&
ShaderSource::GetSourceCode(Api targetApi) const
{
    static sl::Allocator alloc;
    static sl::HLSLGenerator hlsl_gen(&alloc);
    static sl::GLESGenerator gles_gen(&alloc);
    static sl::MSLGenerator mtl_gen(&alloc);
    const char* main = (type == PROG_VERTEX) ? "vp_main" : "fp_main";
    DVASSERT(targetApi < countof(code));
    std::string* src = code + targetApi;

    if (src->length() == 0)
    {
        DVASSERT(ast);

        switch (targetApi)
        {
        case RHI_DX11:
        {
            sl::HLSLGenerator::Target target = (type == PROG_VERTEX) ? sl::HLSLGenerator::Target_VertexShader : sl::HLSLGenerator::Target_PixelShader;

            if (!hlsl_gen.Generate(ast, sl::HLSLGenerator::MODE_DX11, target, main, src))
                src->clear();
        }
        break;

        case RHI_DX9:
        {
            sl::HLSLGenerator::Target target = (type == PROG_VERTEX) ? sl::HLSLGenerator::Target_VertexShader : sl::HLSLGenerator::Target_PixelShader;

            if (hlsl_gen.Generate(ast, sl::HLSLGenerator::MODE_DX9, target, main, src))
                src->clear();
        }
        break;

        case RHI_GLES2:
        {
            sl::GLESGenerator::Target target = (type == PROG_VERTEX) ? sl::GLESGenerator::Target_VertexShader : sl::GLESGenerator::Target_FragmentShader;

            if (!gles_gen.Generate(ast, target, main, src))
                src->clear();
        }
        break;

        case RHI_METAL:
        {
            sl::MSLGenerator::Target target = (type == PROG_VERTEX) ? sl::MSLGenerator::Target_VertexShader : sl::MSLGenerator::Target_PixelShader;

            if (!mtl_gen.Generate(ast, target, main, src))
                src->clear();
        }
        break;
        }
    }

#if 0
{
    Logger::Info("src-code (api=%i) :",int(targetApi));

    char ss[64 * 1024];
    unsigned line_cnt = 0;

    if (strlen(src->c_str()) < sizeof(ss))
    {
        strcpy(ss, src->c_str());

        const char* line = ss;
        for (char* s = ss; *s; ++s)
        {
            if( *s=='\r')
                *s=' ';

            if (*s == '\n')
            {
                *s = 0;
                Logger::Info("%4u |  %s", 1 + line_cnt, line);
                line = s+1;
                ++line_cnt;
            }
        }
    }
    else
    {
        Logger::Info(code->c_str());
    }
}
#endif

    return code[targetApi];
}

//------------------------------------------------------------------------------

const ShaderPropList&
ShaderSource::Properties() const
{
    return property;
}

//------------------------------------------------------------------------------

const ShaderSamplerList&
ShaderSource::Samplers() const
{
    return sampler;
}

//------------------------------------------------------------------------------

const VertexLayout&
ShaderSource::ShaderVertexLayout() const
{
    return vertexLayout;
}

//------------------------------------------------------------------------------

uint32
ShaderSource::ConstBufferCount() const
{
    return static_cast<uint32>(buf.size());
}

//------------------------------------------------------------------------------

uint32
ShaderSource::ConstBufferSize(uint32 bufIndex) const
{
    return buf[bufIndex].regCount;
}


//------------------------------------------------------------------------------

ShaderProp::Source
ShaderSource::ConstBufferSource(uint32 bufIndex) const
{
    return buf[bufIndex].source;
}

//------------------------------------------------------------------------------

BlendState
ShaderSource::Blending() const
{
    return blending;
}

//------------------------------------------------------------------------------

void ShaderSource::Reset()
{
    vertexLayout.Clear();
    property.clear();
    sampler.clear();
    buf.clear();
    //    code.clear();
    codeLineCount = 0;

    for (unsigned i = 0; i != countof(blending.rtBlend); ++i)
    {
        blending.rtBlend[i].blendEnabled = false;
        blending.rtBlend[i].alphaToCoverage = false;
    }

    for (unsigned i = 0; i != countof(code); ++i)
        code[i].clear();
}

//------------------------------------------------------------------------------

void ShaderSource::Dump() const
{
    /*
    Logger::Info("src-code:");

    char src[64 * 1024];
    char* src_line[1024];
    unsigned line_cnt = 0;

    if (strlen(code.c_str()) < sizeof(src))
    {
        strcpy(src, code.c_str());
        memset(src_line, 0, sizeof(src_line));

        src_line[line_cnt++] = src;
        for (char* s = src; *s; ++s)
        {
            if (*s == '\n' || *s == '\r')
            {
                while (*s && (*s == '\n' || *s == '\r'))
                {
                    *s = 0;
                    ++s;
                }

                if (!(*s))
                    break;

                src_line[line_cnt] = s;
                ++line_cnt;
            }
        }

        for (unsigned i = 0; i != line_cnt; ++i)
        {
            Logger::Info("%4u |  %s", 1 + i, src_line[i]);
        }
    }
    else
    {
        Logger::Info(code.c_str());
    }
*/
    Logger::Info("properties (%u) :", property.size());
    for (std::vector<ShaderProp>::const_iterator p = property.begin(), p_end = property.end(); p != p_end; ++p)
    {
        if (p->type == ShaderProp::TYPE_FLOAT4 || p->type == ShaderProp::TYPE_FLOAT4X4)
        {
            if (p->arraySize == 1)
            {
                Logger::Info("  %-16s    buf#%u  -  %u, %u x float4", p->uid.c_str(), p->bufferindex, p->bufferReg, p->bufferRegCount);
            }
            else
            {
                char name[128];

                Snprintf(name, sizeof(name) - 1, "%s[%u]", p->uid.c_str(), p->arraySize);
                Logger::Info("  %-16s    buf#%u  -  %u, %u x float4", name, p->bufferindex, p->bufferReg, p->bufferRegCount);
            }
        }
        else
        {
            const char* xyzw = "xyzw";

            switch (p->type)
            {
            case ShaderProp::TYPE_FLOAT1:
                Logger::Info("  %-16s    buf#%u  -  %u, %c", p->uid.c_str(), p->bufferindex, p->bufferReg, xyzw[p->bufferRegCount]);
                break;

            case ShaderProp::TYPE_FLOAT2:
                Logger::Info("  %-16s    buf#%u  -  %u, %c%c", p->uid.c_str(), p->bufferindex, p->bufferReg, xyzw[p->bufferRegCount + 0], xyzw[p->bufferRegCount + 1]);
                break;

            case ShaderProp::TYPE_FLOAT3:
                Logger::Info("  %-16s    buf#%u  -  %u, %c%c%c", p->uid.c_str(), p->bufferindex, p->bufferReg, xyzw[p->bufferRegCount + 0], xyzw[p->bufferRegCount + 1], xyzw[p->bufferRegCount + 2]);
                break;

            default:
                break;
            }
        }
    }

    Logger::Info("buffers (%u) :", buf.size());
    for (unsigned i = 0; i != buf.size(); ++i)
    {
        Logger::Info("  buf#%u  reg.count = %u", i, buf[i].regCount);
    }

    if (type == PROG_VERTEX)
    {
        Logger::Info("vertex-layout:");
        vertexLayout.Dump();
    }

    Logger::Info("samplers (%u) :", sampler.size());
    for (unsigned s = 0; s != sampler.size(); ++s)
    {
        Logger::Info("  sampler#%u  \"%s\"", s, sampler[s].uid.c_str());
    }
}

//==============================================================================

Mutex shaderSourceEntryMutex;
std::vector<ShaderSourceCache::entry_t> ShaderSourceCache::Entry;
const uint32 ShaderSourceCache::FormatVersion = 5;

const ShaderSource*
ShaderSourceCache::Get(FastName uid, uint32 srcHash)
{
    LockGuard<Mutex> guard(shaderSourceEntryMutex);

    //    Logger::Info("get-shader-src (host-api = %i)",HostApi());
    //    Logger::Info("  uid= \"%s\"",uid.c_str());
    const ShaderSource* src = nullptr;
    Api api = HostApi();

    for (std::vector<entry_t>::const_iterator e = Entry.begin(), e_end = Entry.end(); e != e_end; ++e)
    {
        if (e->uid == uid && e->api == api && e->srcHash == srcHash)
        {
            src = e->src;
            break;
        }
    }
    //    Logger::Info("  %s",(src)?"found":"not found");

    return src;
}

//------------------------------------------------------------------------------
const ShaderSource*
ShaderSourceCache::Add(const char* filename, FastName uid, ProgType progType, const char* srcText, const std::vector<std::string>& defines)
{
    ShaderSource* src = new ShaderSource(filename);

    if (src->Construct(progType, srcText, defines))
    {
        LockGuard<Mutex> guard(shaderSourceEntryMutex);

        entry_t e;

        e.uid = uid;
        e.api = HostApi();
        e.srcHash = DAVA::HashValue_N(srcText, unsigned(strlen(srcText)));
        e.src = src;

        Entry.push_back(e);
    }
    else
    {
        delete src;
        src = nullptr;
    }

    return src;
}

//------------------------------------------------------------------------------

void ShaderSourceCache::Clear()
{
    LockGuard<Mutex> guard(shaderSourceEntryMutex);

    for (std::vector<entry_t>::const_iterator e = Entry.begin(), e_end = Entry.end(); e != e_end; ++e)
        delete e->src;
    Entry.clear();
}

//------------------------------------------------------------------------------

void ShaderSourceCache::Save(const char* fileName)
{
    using namespace DAVA;

    static const FilePath cacheTempFile("~doc:/shader_source_cache_temp.bin");

    File* file = File::Create(cacheTempFile, File::WRITE | File::CREATE);
    if (file)
    {
        Logger::Info("saving cached-shaders (%u): ", Entry.size());

        LockGuard<Mutex> guard(shaderSourceEntryMutex);
        bool success = true;

        SCOPE_EXIT
        {
            SafeRelease(file);

            if (success)
            {
                FileSystem::Instance()->MoveFile(cacheTempFile, fileName, true);
            }
            else
            {
                FileSystem::Instance()->DeleteFile(cacheTempFile);
            }
        };
        
#define WRITE_CHECK(exp) if (!exp) { success = false; return; }

        WRITE_CHECK(WriteUI4(file, FormatVersion));
        WRITE_CHECK(WriteUI4(file, static_cast<uint32>(Entry.size())));
        for (std::vector<entry_t>::const_iterator e = Entry.begin(), e_end = Entry.end(); e != e_end; ++e)
        {
            WRITE_CHECK(WriteS0(file, e->uid.c_str()));
            WRITE_CHECK(WriteUI4(file, e->api));
            WRITE_CHECK(WriteUI4(file, e->srcHash));
            WRITE_CHECK(e->src->Save(Api(e->api), file));
        }
        
#undef WRITE_CHECK
    }
}

//------------------------------------------------------------------------------

void ShaderSourceCache::Load(const char* fileName)
{
    using namespace DAVA;

    ScopedPtr<File> file(File::Create(fileName, File::READ | File::OPEN));

    if (file)
    {
        Clear();

        bool success = true;
        SCOPE_EXIT
        {
            if (!success)
            {
                Clear();
            }
        };
        
#define READ_CHECK(exp) if (!exp) { success = false; return; }

        uint32 readUI4 = 0;
        READ_CHECK(ReadUI4(file, &readUI4));

        if (readUI4 == FormatVersion)
        {
            LockGuard<Mutex> guard(shaderSourceEntryMutex);

            READ_CHECK(ReadUI4(file, &readUI4));
            Entry.resize(readUI4);
            Logger::Info("loading cached-shaders (%u): ", Entry.size());

            for (std::vector<entry_t>::iterator e = Entry.begin(), e_end = Entry.end(); e != e_end; ++e)
            {
                std::string str;
                READ_CHECK(ReadS0(file, &str));

                e->uid = FastName(str.c_str());
                READ_CHECK(ReadUI4(file, &e->api));
                READ_CHECK(ReadUI4(file, &e->srcHash));
                e->src = new ShaderSource();

                READ_CHECK(e->src->Load(Api(e->api), file));
            }
        }
        else
        {
            Logger::Warning("ShaderSource-Cache version mismatch, ignoring cached shaders\n");
            success = false;
        }
        
#undef READ_CHECK
    }
}

//==============================================================================
} // namespace rhi
