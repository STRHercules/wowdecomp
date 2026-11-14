constexpr uint32_t WingPreviewSocket = 0xfffffffcu;
std::string WingPath(const char* name) {
    std::string path=name;
    std::replace(path.begin(),path.end(),'\\','/');
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return char(std::tolower(c));});
    if (path.size()>4 && path.substr(path.size()-4)==".mdx") path.replace(path.size()-4,4,".m2");
    return path;
}

int WingPreview(lua_State* L) {
    auto* frame=static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L,CSimpleModel::GetObjectType()));
    const int spellId=luaL_checkint(L,2);
    auto* parent=frame->m_model;
    if (!parent || !parent->IsLoaded(0,0)) { lua_pushboolean(L,false); return 1; }
    SpellRec spell;
    if (spellId<=0 || !g_spellDB.GetRecord(spellId,&spell)) { lua_pushboolean(L,false); return 1; }
    const auto* visual=GetSpellVisual(&spell);
    if (!visual) { lua_pushboolean(L,false); return 1; }
    const auto& data=*parent->m_shared->m_data;
    if (data.m_attachmentIndicesById.m_count<=16 || data.m_attachmentIndicesById[16]>=data.m_attachments.m_count) {
        lua_pushboolean(L,false); return 1;
    }
    const auto& socket=data.m_attachments[data.m_attachmentIndicesById[16]];
    bool ready=true, found=false;
    for (uint32_t i=0;i<SpellVisualsGetModelAttachRecCount(visual->m_stateKit);++i) {
        const auto* attach=SpellVisualsGetModelAttachRec(visual->m_stateKit,i);
        const auto* effect=attach ? g_spellVisualEffectNameDB.GetRecord(attach->m_spellVisualEffectNameID) : nullptr;
        if (!effect || attach->m_attachmentID!=16) continue;
        found=true;
        const auto path=WingPath(effect->m_fileName);
        auto* child=parent->m_attachList;
        while (child && (child->m_attachmentId!=WingPreviewSocket || !child->m_shared ||
            WingPath(child->m_shared->m_fileName)!=path)) child=child->m_attachNext;
        if (!child) {
            child=parent->m_scene->CreateModel(path.c_str(),0);
            if (!child) { ready=false; continue; }
            child->AttachToParent(parent,WingPreviewSocket,nullptr,1);
            child->SetAttachmentFrame(socket.m_bone,socket.m_position,1.0f);
            C44Matrix local(effect->m_scale);
            local.RotateAroundX(attach->m_roll);
            local.RotateAroundY(attach->m_pitch);
            local.RotateAroundZ(attach->m_yaw);
            local.Translate(C3Vector(attach->m_offset[0],attach->m_offset[1],attach->m_offset[2]));
            child->SetWorldTransform(local);
            child->Release();
        }
        if (child->IsLoaded(0,0)) {
            child->SetEmittersEnabled(1);
            child->SetRibbonsEnabled(1);
        } else ready=false;
    }
    lua_pushboolean(L,found && ready);
    return 1;
}

int CosmeticFit(lua_State* L) {
    auto* frame=static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L,CSimpleModel::GetObjectType()));
    const float facing=static_cast<float>(luaL_optnumber(L,2,0));
    const float zoom=std::max(0.2f,std::min(2.0f,static_cast<float>(luaL_optnumber(L,3,1))));
    auto* parent=frame->m_model;
    if (!parent || !parent->IsLoaded(0,0) || !std::isfinite(facing)) { lua_pushboolean(L,false); return 1; }
    // Hidden loading frames still need a pose before measuring animated scale and child attachments.
    parent->ForceAnimate();
    if (!parent->m_boneMatrices || !std::isfinite(parent->m_matrixF4.Determinant()) ||
        std::fabs(parent->m_matrixF4.Determinant())<1e-12f) { lua_pushboolean(L,false); return 1; }
    const C44Matrix toParent=parent->m_matrixF4.Inverse(parent->m_matrixF4.Determinant());
    C3Vector low(1e9f,1e9f,1e9f),high(-1e9f,-1e9f,-1e9f);
    const auto add=[&](const C3Vector& p) {
        low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); low.z=std::min(low.z,p.z);
        high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); high.z=std::max(high.z,p.z);
    };
    const auto include=[&](CM2Model* model) {
        const auto& data=*model->m_shared->m_data;
        const auto* skin=model->m_shared->m_skinData;
        bool sampled=false;
        if (skin && model->m_skinSections && model->m_boneMatrices)
          for (uint32_t i=0;i<skin->m_skinSections.m_count;++i) {
            if (!model->m_skinSections[i]) continue;
            const auto& section=skin->m_skinSections[i];
            const uint32_t end=uint32_t(section.m_vertexStart)+section.m_vertexCount;
            if (end>skin->m_vertices.m_count) continue;
            for (uint32_t n=section.m_vertexStart;n<end;++n) {
                if (skin->m_vertices[n]>=data.m_vertices.m_count) continue;
                const auto& vertex=data.m_vertices[skin->m_vertices[n]];
                C3Vector posed(0,0,0);
                unsigned total=0;
                for (unsigned k=0;k<4;++k) {
                    if (!vertex.m_weights[k] || vertex.m_indices[k]>=data.m_bones.m_count) continue;
                    const auto p=vertex.m_position*model->m_boneMatrices[vertex.m_indices[k]];
                    const float weight=vertex.m_weights[k]/255.0f;
                    posed.x+=p.x*weight; posed.y+=p.y*weight; posed.z+=p.z*weight;
                    total+=vertex.m_weights[k];
                }
                if (total) { add(posed*toParent); sampled=true; }
            }
          }
        if (!sampled) {
            const C44Matrix transform=model->m_matrixF4*toParent;
            for (unsigned mask=0;mask<8;++mask) add(C3Vector(
                mask&1 ? data.m_bounds.m_extent.t.x : data.m_bounds.m_extent.b.x,
                mask&2 ? data.m_bounds.m_extent.t.y : data.m_bounds.m_extent.b.y,
                mask&4 ? data.m_bounds.m_extent.t.z : data.m_bounds.m_extent.b.z)*transform);
        }
    };
    include(parent);
    bool ready=true;
    for (auto* child=parent->m_attachList;child;child=child->m_attachNext) {
        if (child->m_attachmentId!=WingPreviewSocket && child->m_attachmentId!=0xfffffffdu) continue;
        if (!child->IsLoaded(0,0)) { ready=false; continue; }
        include(child);
    }
    if (!ready) { lua_pushboolean(L,false); return 1; }
    const auto fit=Retail::FitCosmeticBounds({low.x,low.y,low.z},{high.x,high.y,high.z},facing,
        frame->GetWidth()/std::max(0.01f,frame->GetHeight()),zoom);
    const C3Vector center(fit.center[0],fit.center[1],fit.center[2]);
    const C3Vector direction(0.93f,-0.32f,0.18f);
    const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    frame->m_facing=facing;
    const float uiScale=frame->m_layoutScale*NDCToDDCHeight(1.0f)*1.6666666f;
    frame->m_modelScale=uiScale>0 ? 1.0f/uiScale : 1.0f;
    frame->m_position=C3Vector(0,0,0);
    if (auto* camera=CameraCreate()) {
        CameraSetCoord(camera,7,C3Vector(center.x+direction.x/length*fit.distance,
            center.y+direction.y/length*fit.distance,center.z+direction.z/length*fit.distance));
        CameraSetCoord(camera,8,center);
        CameraSetFloat(camera,4,0.8f);
        CameraSetFloat(camera,3,std::max(0.001f,fit.radius*0.001f));
        CameraSetFloat(camera,2,std::max(20.0f,fit.distance+fit.radius*10));
        frame->SetCamera(camera);
        HandleClose(reinterpret_cast<HOBJECT>(camera));
    }
    frame->m_light.SetLightType(M2LIGHT_DIRECT);
    frame->m_light.SetDirection(C3Vector(-0.5f,-0.8f,-1));
    frame->m_light.m_ambColor=C3Vector(0.75f,0.75f,0.75f);
    frame->m_light.m_dirColor=C3Vector(0.65f,0.65f,0.65f);
    frame->m_light.m_specColor=C3Vector(0.2f,0.2f,0.2f);
    frame->m_light.SetVisible(1);
    parent->SetEmittersEnabled(1);
    parent->SetRibbonsEnabled(1);
    lua_pushboolean(L,true);
    return 1;
}
