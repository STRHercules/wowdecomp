struct PickaxeFitValues { float scale=1,x=0,y=0,z=0,pitch=0,yaw=0,roll=0; };
std::map<std::tuple<std::string,uint32_t,bool>,PickaxeFitValues> pickaxeFits;
std::set<CM2Model*> pickaxeModels;
uint32_t PickaxeModelDisplay(const CM2Model* model) {
    return model && model->m_shared ? Retail::PickaxeDisplay(WingPath(model->m_shared->m_fileName)) : 0;
}
bool IsPickaxeModel(const CM2Model* model) {
    return PickaxeModelDisplay(model)!=0;
}
void ApplyPickaxeFit(CM2Model* model) {
    auto* parent=model ? model->m_attachParent : nullptr;
    if (!parent || !parent->m_shared) return;
    const bool sheathed=model->m_attachmentId!=1 && model->m_attachmentId!=2;
    const auto found=pickaxeFits.find({WingPath(parent->m_shared->m_fileName),PickaxeModelDisplay(model),sheathed});
    const auto values=Retail::PickaxeDefaultFit(PickaxeModelDisplay(model),sheathed);
    const PickaxeFitValues defaults{values[0],values[1],values[2],values[3],values[4],values[5],values[6]};
    const auto& fit=found==pickaxeFits.end() ? defaults : found->second;
    C44Matrix matrix(fit.scale);
    constexpr float radians=3.14159265359f/180;
    matrix.RotateAroundX(fit.pitch*radians);matrix.RotateAroundY(fit.yaw*radians);matrix.RotateAroundZ(fit.roll*radians);
    matrix.d0=fit.x;matrix.d1=fit.y;matrix.d2=fit.z;
    model->SetWorldTransform(matrix);
}
int PickaxeFitInfo(lua_State* L) {
    auto* player=ClntObjMgrGetActivePlayerObj();auto* parent=player ? player->m_worldModel : nullptr;
    if (!parent || !parent->m_shared) return 0;
    const uint32_t requested=uint32_t(luaL_optnumber(L,1,0));
    for (auto* child=parent->m_attachList;child;child=child->m_attachNext) if (IsPickaxeModel(child) &&
        (!requested || requested==PickaxeModelDisplay(child))) {
        lua_pushinteger(L,PickaxeModelDisplay(child));lua_pushstring(L,WingPath(parent->m_shared->m_fileName).c_str());
        lua_pushboolean(L,child->m_attachmentId!=1 && child->m_attachmentId!=2);return 3;
    }
    return 0;
}
int PickaxeDefaultFit(lua_State* L) {
    const auto fit=Retail::PickaxeDefaultFit(uint32_t(luaL_checkint(L,1)),lua_toboolean(L,2)!=0);
    for (float value:fit)lua_pushnumber(L,value);
    return 7;
}
int PickaxeSetFit(lua_State* L) {
    const int display=luaL_checkint(L,1);const std::string body=WingPath(luaL_checkstring(L,2));
    PickaxeFitValues fit;
    float* fields[]={&fit.scale,&fit.x,&fit.y,&fit.z,&fit.pitch,&fit.yaw,&fit.roll};
    bool valid=Retail::IsPickaxeDisplay(uint32_t(display)) && !body.empty() && body.size()<=240 && body.find_first_of(";\r\n")==std::string::npos;
    for (unsigned i=0;i<7;++i) {
        *fields[i]=float(luaL_checknumber(L,3+i));
        valid=valid && std::isfinite(*fields[i]) && (i==0 ? *fields[i]>=0.1f && *fields[i]<=4 : std::fabs(*fields[i])<=(i<4 ? 3.0f : 180.0f));
    }
    const bool sheathed=lua_toboolean(L,10)!=0;
    if (valid) {pickaxeFits[{body,uint32_t(display),sheathed}]=fit;for(auto* model:pickaxeModels) ApplyPickaxeFit(model);}
    lua_pushboolean(L,valid);return 1;
}
class PickaxeFitModels:public ModelScript {
public:
    PickaxeFitModels():ModelScript("PickaxeFitModels"){}
    void OnModelCreated(CM2Model& model) override {if(IsPickaxeModel(&model))pickaxeModels.insert(&model);}
    void OnModelDestroy(CM2Model& model) override {pickaxeModels.erase(&model);}
};
