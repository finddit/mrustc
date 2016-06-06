/*
 */
#include "main_bindings.hpp"
#include <hir/expr.hpp>
#include <hir/hir.hpp>
#include <hir/visitor.hpp>
#include <algorithm>    // std::find_if

namespace {
    
    bool monomorphise_type_needed(const ::HIR::TypeRef& tpl);
    
    bool monomorphise_pathparams_needed(const ::HIR::PathParams& tpl)
    {
        for(const auto& ty : tpl.m_types)
            if( monomorphise_type_needed(ty) )
                return true;
        return false;
    }
    bool monomorphise_path_needed(const ::HIR::Path& tpl)
    {
        TU_MATCH(::HIR::Path::Data, (tpl.m_data), (e),
        (Generic,
            return monomorphise_pathparams_needed(e.m_params);
            ),
        (UfcsInherent,
            return monomorphise_type_needed(*e.type) || monomorphise_pathparams_needed(e.params);
            ),
        (UfcsKnown,
            return monomorphise_type_needed(*e.type) || monomorphise_pathparams_needed(e.trait.m_params) || monomorphise_pathparams_needed(e.params);
            ),
        (UfcsUnknown,
            return monomorphise_type_needed(*e.type) || monomorphise_pathparams_needed(e.params);
            )
        )
        throw "";
    }
    bool monomorphise_type_needed(const ::HIR::TypeRef& tpl)
    {
        TU_MATCH(::HIR::TypeRef::Data, (tpl.m_data), (e),
        (Infer,
            assert(!"ERROR: _ type found in monomorphisation target");
            ),
        (Diverge,
            return false;
            ),
        (Primitive,
            return false;
            ),
        (Path,
            return monomorphise_path_needed(e.path);
            ),
        (Generic,
            return true;
            ),
        (TraitObject,
            TODO(Span(), "TraitObject - " << tpl);
            ),
        (Array,
            TODO(Span(), "Array - " << tpl);
            ),
        (Slice,
            return monomorphise_type_needed(*e.inner);
            ),
        (Tuple,
            for(const auto& ty : e) {
                if( monomorphise_type_needed(ty) )
                    return true;
            }
            return false;
            ),
        (Borrow,
            return monomorphise_type_needed(*e.inner);
            ),
        (Pointer,
            return monomorphise_type_needed(*e.inner);
            ),
        (Function,
            TODO(Span(), "Function - " << tpl);
            )
        )
        throw "";
    }
    typedef ::std::function<const ::HIR::TypeRef&(const ::HIR::TypeRef&)>   t_cb_generic;
    ::HIR::TypeRef monomorphise_type_with(const Span& sp, const ::HIR::TypeRef& tpl, t_cb_generic callback, bool allow_infer=true);
    
    ::HIR::PathParams monomorphise_path_params_with(const Span& sp, const ::HIR::PathParams& tpl, t_cb_generic callback, bool allow_infer)
    {
        ::HIR::PathParams   rv;
        for( const auto& ty : tpl.m_types) 
            rv.m_types.push_back( monomorphise_type_with(sp, ty, callback) );
        return rv;
    }
    ::HIR::GenericPath monomorphise_genericpath_with(const Span& sp, const ::HIR::GenericPath& tpl, t_cb_generic callback, bool allow_infer)
    {
        return ::HIR::GenericPath( tpl.m_path, monomorphise_path_params_with(sp, tpl.m_params, callback, allow_infer) );
    }
    ::HIR::TypeRef monomorphise_type_with(const Span& sp, const ::HIR::TypeRef& tpl, t_cb_generic callback, bool allow_infer)
    {
        TRACE_FUNCTION_F("tpl = " << tpl);
        TU_MATCH(::HIR::TypeRef::Data, (tpl.m_data), (e),
        (Infer,
            if( allow_infer ) {
                return ::HIR::TypeRef(e);
            }
            else {
               BUG(sp, "_ type found in monomorphisation target");
            }
            ),
        (Diverge,
            return ::HIR::TypeRef(e);
            ),
        (Primitive,
            return ::HIR::TypeRef(e);
            ),
        (Path,
            TU_MATCH(::HIR::Path::Data, (e.path.m_data), (e2),
            (Generic,
                return ::HIR::TypeRef( monomorphise_genericpath_with(sp, e2, callback, allow_infer) );
                ),
            (UfcsKnown,
                return ::HIR::TypeRef( ::HIR::Path::Data::make_UfcsKnown({
                    box$( monomorphise_type_with(sp, *e2.type, callback, allow_infer) ),
                    monomorphise_genericpath_with(sp, e2.trait, callback, allow_infer),
                    e2.item,
                    monomorphise_path_params_with(sp, e2.params, callback, allow_infer)
                    }) );
                ),
            (UfcsUnknown,
                TODO(sp, "UfcsUnknown");
                ),
            (UfcsInherent,
                TODO(sp, "UfcsInherent");
                )
            )
            ),
        (Generic,
            return callback(tpl).clone();
            ),
        (TraitObject,
            TODO(sp, "TraitObject");
            ),
        (Array,
            if( e.size_val == ~0u ) {
                BUG(sp, "Attempting to clone array with unknown size - " << tpl);
            }
            return ::HIR::TypeRef( ::HIR::TypeRef::Data::make_Array({
                box$( monomorphise_type_with(sp, *e.inner, callback) ),
                ::HIR::ExprPtr(),
                e.size_val
                }) );
            ),
        (Slice,
            return ::HIR::TypeRef( ::HIR::TypeRef::Data::make_Slice({ box$(monomorphise_type_with(sp, *e.inner, callback)) }) );
            ),
        (Tuple,
            ::std::vector< ::HIR::TypeRef>  types;
            for(const auto& ty : e) {
                types.push_back( monomorphise_type_with(sp, ty, callback) );
            }
            return ::HIR::TypeRef( mv$(types) );
            ),
        (Borrow,
            return ::HIR::TypeRef::new_borrow(e.type, monomorphise_type_with(sp, *e.inner, callback));
            ),
        (Pointer,
            return ::HIR::TypeRef::new_pointer(e.is_mut, monomorphise_type_with(sp, *e.inner, callback));
            ),
        (Function,
            TODO(sp, "Function");
            )
        )
        throw "";
        
    }
    
    ::HIR::TypeRef monomorphise_type(const Span& sp, const ::HIR::GenericParams& params_def, const ::HIR::PathParams& params,  const ::HIR::TypeRef& tpl)
    {
        DEBUG("tpl = " << tpl);
        return monomorphise_type_with(sp, tpl, [&](const auto& gt)->const auto& {
            const auto& e = gt.m_data.as_Generic();
            if( e.name == "Self" )
                TODO(sp, "Handle 'Self' when monomorphising");
            //if( e.binding >= params_def.m_types.size() ) {
            //}
            if( e.binding >= params.m_types.size() ) {
                BUG(sp, "Generic param out of input range - " << e.binding << " '"<<e.name<<"' >= " << params.m_types.size());
            }
            return params.m_types[e.binding];
            }, false);
    }
    
    struct IVar
    {
        bool    deleted;
        unsigned int alias;
        ::std::unique_ptr< ::HIR::TypeRef> type;
        
        IVar():
            deleted(false),
            alias(~0u),
            type(new ::HIR::TypeRef())
        {}
        bool is_alias() const { return alias != ~0u; }
    };
    static const ::std::string EMPTY_STRING;
    struct Variable
    {
        ::std::string   name;
        ::HIR::TypeRef  type;
        
        Variable()
        {}
        Variable(const ::std::string& name, ::HIR::TypeRef type):
            name( name ),
            type( mv$(type) )
        {}
        Variable(Variable&&) = default;
        
        Variable& operator=(Variable&&) = default;
    };
    
    class TypecheckContext
    {
    public:
        const ::HIR::Crate& m_crate;
        ::std::vector< ::std::pair< const ::HIR::SimplePath*, const ::HIR::Trait* > >   m_traits;
    private:
        ::std::vector< Variable>    m_locals;
        ::std::vector< IVar>    m_ivars;
        bool    m_has_changed;
        
        const ::HIR::GenericParams* m_impl_params;
        const ::HIR::GenericParams* m_item_params;
        
    public:
        TypecheckContext(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::GenericParams* item_params):
            m_crate(crate),
            m_has_changed(false),
            m_impl_params( impl_params ),
            m_item_params( item_params )
        {
        }
        
        void dump() const {
            DEBUG("TypecheckContext - " << m_ivars.size() << " ivars, " << m_locals.size() << " locals");
            unsigned int i = 0;
            for(const auto& v : m_ivars) {
                if(v.is_alias()) {
                    DEBUG("#" << i << " = " << v.alias);
                }
                else {
                    DEBUG("#" << i << " = " << *v.type);
                }
                i ++ ;
            }
            i = 0;
            for(const auto& v : m_locals) {
                DEBUG("VAR " << i << " '"<<v.name<<"' = " << v.type);
                i ++;
            }
        }
        
        bool take_changed() {
            bool rv = m_has_changed;
            m_has_changed = false;
            return rv;
        }
        void mark_change() {
            m_has_changed = true;
        }
        
        /// Adds a local variable binding (type is mutable so it can be inferred if required)
        void add_local(unsigned int index, const ::std::string& name, ::HIR::TypeRef type)
        {
            if( m_locals.size() <= index )
                m_locals.resize(index+1);
            m_locals[index] = Variable(name, mv$(type));
        }

        const ::HIR::TypeRef& get_var_type(const Span& sp, unsigned int index)
        {
            if( index >= m_locals.size() ) {
                this->dump();
                BUG(sp, "Local index out of range " << index << " >= " << m_locals.size());
            }
            return m_locals.at(index).type;
        }

        /// Add (and bind) all '_' types in `type`
        void add_ivars(::HIR::TypeRef& type)
        {
            TU_MATCH(::HIR::TypeRef::Data, (type.m_data), (e),
            (Infer,
                if( e.index == ~0u ) {
                    e.index = this->new_ivar();
                }
                ),
            (Diverge,
                ),
            (Primitive,
                ),
            (Path,
                // Iterate all arguments
                TU_MATCH(::HIR::Path::Data, (e.path.m_data), (e2),
                (Generic,
                    this->add_ivars_params(e2.m_params);
                    ),
                (UfcsKnown,
                    this->add_ivars(*e2.type);
                    this->add_ivars_params(e2.trait.m_params);
                    this->add_ivars_params(e2.params);
                    ),
                (UfcsUnknown,
                    this->add_ivars(*e2.type);
                    this->add_ivars_params(e2.params);
                    ),
                (UfcsInherent,
                    this->add_ivars(*e2.type);
                    this->add_ivars_params(e2.params);
                    )
                )
                ),
            (Generic,
                ),
            (TraitObject,
                // Iterate all paths
                ),
            (Array,
                add_ivars(*e.inner);
                ),
            (Slice,
                add_ivars(*e.inner);
                ),
            (Tuple,
                for(auto& ty : e)
                    add_ivars(ty);
                ),
            (Borrow,
                add_ivars(*e.inner);
                ),
            (Pointer,
                add_ivars(*e.inner);
                ),
            (Function,
                // No ivars allowed
                // TODO: Check?
                )
            )
        }
        void add_ivars_params(::HIR::PathParams& params)
        {
            for(auto& arg : params.m_types)
                add_ivars(arg);
        }
        
        
        void add_pattern_binding(const ::HIR::PatternBinding& pb, ::HIR::TypeRef type)
        {
            assert( pb.is_valid() );
            switch( pb.m_type )
            {
            case ::HIR::PatternBinding::Type::Move:
                this->add_local( pb.m_slot, pb.m_name, mv$(type) );
                break;
            case ::HIR::PatternBinding::Type::Ref:
                this->add_local( pb.m_slot, pb.m_name, ::HIR::TypeRef::new_borrow(::HIR::BorrowType::Shared, mv$(type)) );
                break;
            case ::HIR::PatternBinding::Type::MutRef:
                this->add_local( pb.m_slot, pb.m_name, ::HIR::TypeRef::new_borrow(::HIR::BorrowType::Unique, mv$(type)) );
                break;
            }
        }
        
        void add_binding(const Span& sp, ::HIR::Pattern& pat, ::HIR::TypeRef& type)
        {
            TRACE_FUNCTION_F("pat = " << pat << ", type = " << type);
            
            if( pat.m_binding.is_valid() ) {
                this->add_pattern_binding(pat.m_binding, type.clone());
                // TODO: Can there be bindings within a bound pattern?
                //return ;
            }
            
            // 
            TU_MATCH(::HIR::Pattern::Data, (pat.m_data), (e),
            (Any,
                // Just leave it, the pattern says nothing
                ),
            (Value,
                //TODO(sp, "Value pattern");
                ),
            (Range,
                //TODO(sp, "Range pattern");
                ),
            (Box,
                TODO(sp, "Box pattern");
                ),
            (Ref,
                if( type.m_data.is_Infer() ) {
                    type.m_data = ::HIR::TypeRef::Data::make_Borrow({ e.type, box$(this->new_ivar_tr()) });
                }
                // Type must be a &-ptr
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Borrow,
                    if( te.type != e.type ) {
                        // TODO: Type mismatch
                    }
                    this->add_binding(sp, *e.sub, *te.inner );
                    )
                )
                ),
            (Tuple,
                if( type.m_data.is_Infer() ) {
                    ::std::vector< ::HIR::TypeRef>  sub_types;
                    for(unsigned int i = 0; i < e.sub_patterns.size(); i ++ )
                        sub_types.push_back( this->new_ivar_tr() );
                    type.m_data = ::HIR::TypeRef::Data::make_Tuple( mv$(sub_types) );
                }
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Tuple,
                    if( te.size() != e.sub_patterns.size() ) {
                        // TODO: Type mismatch
                    }
                    for(unsigned int i = 0; i < e.sub_patterns.size(); i ++ )
                        this->add_binding(sp, e.sub_patterns[i], te[i] );
                    )
                )
                ),
            (Slice,
                if( type.m_data.is_Infer() ) {
                    type.m_data = ::HIR::TypeRef::Data::make_Slice( {box$(this->new_ivar_tr())} );
                    this->mark_change();
                }
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw""; ),
                (Slice,
                    for(auto& sub : e.sub_patterns)
                        this->add_binding(sp, sub, *te.inner );
                    )
                )
                ),
            (SplitSlice,
                if( type.m_data.is_Infer() ) {
                    type.m_data = ::HIR::TypeRef::Data::make_Slice( {box$(this->new_ivar_tr())} );
                    this->mark_change();
                }
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Slice,
                    for(auto& sub : e.leading)
                        this->add_binding( sp, sub, *te.inner );
                    for(auto& sub : e.trailing)
                        this->add_binding( sp, sub, *te.inner );
                    if( e.extra_bind.is_valid() ) {
                        this->add_local( e.extra_bind.m_slot, e.extra_bind.m_name, type.clone() );
                    }
                    )
                )
                ),
            
            // - Enums/Structs
            (StructTuple,
                this->add_ivars_params( e.path.m_params );
                if( type.m_data.is_Infer() ) {
                    type.m_data = ::HIR::TypeRef::Data::make_Path( {e.path.clone(), ::HIR::TypeRef::TypePathBinding(e.binding)} );
                }
                assert(e.binding);
                const auto& str = *e.binding;
                // - assert check from earlier pass
                assert( str.m_data.is_Tuple() );
                const auto& sd = str.m_data.as_Tuple();
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Path,
                    if( ! te.binding.is_Struct() || te.binding.as_Struct() != &str ) {
                        ERROR(sp, E0000, "Type mismatch in struct pattern - " << type << " is not " << e.path);
                    }
                    // NOTE: Must be Generic for the above to have passed
                    auto& gp = te.path.m_data.as_Generic();
                    
                    if( e.sub_patterns.size() != sd.size() ) { 
                        ERROR(sp, E0000, "Tuple struct pattern with an incorrect number of fields");
                    }
                    for( unsigned int i = 0; i < e.sub_patterns.size(); i ++ )
                    {
                        const auto& field_type = sd[i].ent;
                        if( monomorphise_type_needed(field_type) ) {
                            auto var_ty = monomorphise_type(sp, str.m_params, gp.m_params,  field_type);
                            this->add_binding(sp, e.sub_patterns[i], var_ty);
                        }
                        else {
                            // SAFE: Can't have _ as monomorphise_type_needed checks for that
                            this->add_binding(sp, e.sub_patterns[i], const_cast< ::HIR::TypeRef&>(field_type));
                        }
                    }
                    )
                )
                ),
            (StructTupleWildcard,
                this->add_ivars_params( e.path.m_params );
                if( type.m_data.is_Infer() ) {
                    type.m_data = ::HIR::TypeRef::Data::make_Path( {e.path.clone(), ::HIR::TypeRef::TypePathBinding(e.binding)} );
                }
                assert(e.binding);
                const auto& str = *e.binding;
                // - assert check from earlier pass
                assert( str.m_data.is_Tuple() );
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Path,
                    if( ! te.binding.is_Struct() || te.binding.as_Struct() != &str ) {
                        ERROR(sp, E0000, "Type mismatch in struct pattern - " << type << " is not " << e.path);
                    }
                    )
                )
                ),
            (Struct,
                this->add_ivars_params( e.path.m_params );
                if( type.m_data.is_Infer() ) {
                    type.m_data = ::HIR::TypeRef::Data::make_Path( {e.path.clone(), ::HIR::TypeRef::TypePathBinding(e.binding)} );
                }
                assert(e.binding);
                const auto& str = *e.binding;
                // - assert check from earlier pass
                assert( str.m_data.is_Named() );
                const auto& sd = str.m_data.as_Named();
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Path,
                    if( ! te.binding.is_Struct() || te.binding.as_Struct() != &str ) {
                        ERROR(sp, E0000, "Type mismatch in struct pattern - " << type << " is not " << e.path);
                    }
                    // NOTE: Must be Generic for the above to have passed
                    auto& gp = te.path.m_data.as_Generic();
                    for( auto& field_pat : e.sub_patterns )
                    {
                        unsigned int f_idx = ::std::find_if( sd.begin(), sd.end(), [&](const auto& x){ return x.first == field_pat.first; } ) - sd.begin();
                        if( f_idx == sd.size() ) {
                            ERROR(sp, E0000, "Struct " << e.path << " doesn't have a field " << field_pat.first);
                        }
                        const ::HIR::TypeRef& field_type = sd[f_idx].second.ent;
                        if( monomorphise_type_needed(field_type) ) {
                            auto field_type_mono = monomorphise_type(sp, str.m_params, gp.m_params,  field_type);
                            this->add_binding(sp, field_pat.second, field_type_mono);
                        }
                        else {
                            // SAFE: Can't have _ as monomorphise_type_needed checks for that
                            this->add_binding(sp, field_pat.second, const_cast< ::HIR::TypeRef&>(field_type));
                        }
                    }
                    )
                )
                ),
            (EnumTuple,
                this->add_ivars_params( e.path.m_params );
                if( type.m_data.is_Infer() ) {
                    auto path = e.path.clone();
                    path.m_path.m_components.pop_back();
                    type.m_data = ::HIR::TypeRef::Data::make_Path( {mv$(path), ::HIR::TypeRef::TypePathBinding(e.binding_ptr)} );
                }
                assert(e.binding_ptr);
                const auto& enm = *e.binding_ptr;
                const auto& var = enm.m_variants[e.binding_idx].second;
                assert(var.is_Tuple());
                const auto& tup_var = var.as_Tuple();
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Path,
                    if( ! te.binding.is_Enum() || te.binding.as_Enum() != &enm ) {
                        ERROR(sp, E0000, "Type mismatch in enum pattern - " << type << " is not " << e.path);
                    }
                    // NOTE: Must be Generic for the above to have passed
                    auto& gp = te.path.m_data.as_Generic();
                    if( e.sub_patterns.size() != tup_var.size() ) { 
                        ERROR(sp, E0000, "Enum pattern with an incorrect number of fields - " << e.path << " - expected " << tup_var.size() << ", got " << e.sub_patterns.size());
                    }
                    for( unsigned int i = 0; i < e.sub_patterns.size(); i ++ )
                    {
                        if( monomorphise_type_needed(tup_var[i]) ) {
                            auto var_ty = monomorphise_type(sp, enm.m_params, gp.m_params,  tup_var[i]);
                            this->add_binding(sp, e.sub_patterns[i], var_ty);
                        }
                        else {
                            // SAFE: Can't have a _ (monomorphise_type_needed checks for that)
                            this->add_binding(sp, e.sub_patterns[i], const_cast< ::HIR::TypeRef&>(tup_var[i]));
                        }
                    }
                    )
                )
                ),
            (EnumTupleWildcard,
                this->add_ivars_params( e.path.m_params );
                if( type.m_data.is_Infer() ) {
                    auto path = e.path.clone();
                    path.m_path.m_components.pop_back();
                    type.m_data = ::HIR::TypeRef::Data::make_Path( {mv$(path), ::HIR::TypeRef::TypePathBinding(e.binding_ptr)} );
                }
                assert(e.binding_ptr);
                const auto& enm = *e.binding_ptr;
                const auto& var = enm.m_variants[e.binding_idx].second;
                assert(var.is_Tuple());
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Path,
                    if( ! te.binding.is_Enum() || te.binding.as_Enum() != &enm ) {
                        ERROR(sp, E0000, "Type mismatch in enum pattern - " << type << " is not " << e.path);
                    }
                    )
                )
                ),
            (EnumStruct,
                this->add_ivars_params( e.path.m_params );
                if( type.m_data.is_Infer() ) {
                    auto path = e.path.clone();
                    path.m_path.m_components.pop_back();
                    type.m_data = ::HIR::TypeRef::Data::make_Path( {mv$(path), ::HIR::TypeRef::TypePathBinding(e.binding_ptr)} );
                }
                assert(e.binding_ptr);
                const auto& enm = *e.binding_ptr;
                const auto& var = enm.m_variants[e.binding_idx].second;
                assert(var.is_Struct());
                const auto& tup_var = var.as_Struct();
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (type.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw ""; ),
                (Path,
                    if( ! te.binding.is_Enum() || te.binding.as_Enum() != &enm ) {
                        ERROR(sp, E0000, "Type mismatch in enum pattern - " << type << " is not " << e.path);
                    }
                    // NOTE: Must be Generic for the above to have passed
                    auto& gp = te.path.m_data.as_Generic();
                    
                    for( auto& field_pat : e.sub_patterns )
                    {
                        unsigned int f_idx = ::std::find_if( tup_var.begin(), tup_var.end(), [&](const auto& x){ return x.first == field_pat.first; } ) - tup_var.begin();
                        if( f_idx == tup_var.size() ) {
                            ERROR(sp, E0000, "Enum variant " << e.path << " doesn't have a field " << field_pat.first);
                        }
                        const ::HIR::TypeRef& field_type = tup_var[f_idx].second;
                        if( monomorphise_type_needed(field_type) ) {
                            auto field_type_mono = monomorphise_type(sp, enm.m_params, gp.m_params,  field_type);
                            this->add_binding(sp, field_pat.second, field_type_mono);
                        }
                        else {
                            // SAFE: Can't have _ as monomorphise_type_needed checks for that
                            this->add_binding(sp, field_pat.second, const_cast< ::HIR::TypeRef&>(field_type));
                        }
                    }
                    )
                )
                )
            )
        }
        
        /// Run inferrence using a pattern
        void apply_pattern(const ::HIR::Pattern& pat, ::HIR::TypeRef& type)
        {
            static Span _sp;
            const Span& sp = _sp;
            // TODO: Should this do an equality on the binding?

            auto& ty = this->get_type(type);
            
            TU_MATCH(::HIR::Pattern::Data, (pat.m_data), (e),
            (Any,
                // Just leave it, the pattern says nothing about the type
                ),
            (Value,
                TODO(sp, "Value pattern");
                ),
            (Range,
                TODO(sp, "Range pattern");
                ),
            // - Pointer destructuring
            (Box,
                // Type must be box-able
                TODO(sp, "Box patterns");
                ),
            (Ref,
                if( ty.m_data.is_Infer() ) {
                    BUG(sp, "Infer type hit that should already have been fixed");
                }
                // Type must be a &-ptr
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Borrow,
                    if( te.type != e.type ) {
                        // TODO: Type mismatch
                    }
                    this->apply_pattern( *e.sub, *te.inner );
                    )
                )
                ),
            (Tuple,
                if( ty.m_data.is_Infer() ) {
                    BUG(sp, "Infer type hit that should already have been fixed");
                }
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Tuple,
                    if( te.size() != e.sub_patterns.size() ) {
                        // TODO: Type mismatch
                    }
                    for(unsigned int i = 0; i < e.sub_patterns.size(); i ++ )
                        this->apply_pattern( e.sub_patterns[i], te[i] );
                    )
                )
                ),
            // --- Slices
            (Slice,
                if( ty.m_data.is_Infer() ) {
                    BUG(sp, "Infer type hit that should already have been fixed");
                }
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Slice,
                    for(const auto& sp : e.sub_patterns )
                        this->apply_pattern( sp, *te.inner );
                    )
                )
                ),
            (SplitSlice,
                if( ty.m_data.is_Infer() ) {
                    BUG(sp, "Infer type hit that should already have been fixed");
                }
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Slice,
                    for(const auto& sp : e.leading)
                        this->apply_pattern( sp, *te.inner );
                    for(const auto& sp : e.trailing)
                        this->apply_pattern( sp, *te.inner );
                    // TODO: extra_bind? (see comment at start of function)
                    )
                )
                ),
            
            // - Enums/Structs
            (StructTuple,
                if( ty.m_data.is_Infer() ) {
                    ty.m_data = ::HIR::TypeRef::Data::make_Path( {e.path.clone(), ::HIR::TypeRef::TypePathBinding(e.binding)} );
                    this->mark_change();
                }
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Path,
                    // TODO: Does anything need to happen here? This can only introduce equalities?
                    )
                )
                ),
            (StructTupleWildcard,
                ),
            (Struct,
                if( ty.m_data.is_Infer() ) {
                    //TODO: Does this lead to issues with generic parameters?
                    ty.m_data = ::HIR::TypeRef::Data::make_Path( {e.path.clone(), ::HIR::TypeRef::TypePathBinding(e.binding)} );
                    this->mark_change();
                }
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Path,
                    // TODO: Does anything need to happen here? This can only introduce equalities?
                    )
                )
                ),
            (EnumTuple,
                if( ty.m_data.is_Infer() ) {
                    TODO(sp, "EnumTuple - infer");
                    this->mark_change();
                }
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Path,
                    )
                )
                ),
            (EnumTupleWildcard,
                ),
            (EnumStruct,
                if( ty.m_data.is_Infer() ) {
                    TODO(sp, "EnumStruct - infer");
                    this->mark_change();
                }
                
                TU_MATCH_DEF(::HIR::TypeRef::Data, (ty.m_data), (te),
                (
                    // TODO: Type mismatch
                    ),
                (Infer, throw "";),
                (Path,
                    )
                )
                )
            )
        }
        // Adds a rule that two types must be equal
        // - NOTE: The ordering does matter, as the righthand side will get unsizing/deref coercions applied if possible
        /// \param sp   Span for reporting errors
        /// \param left     Lefthand type (destination for coercions)
        /// \param right    Righthand type (source for coercions)
        /// \param node_ptr Pointer to ExprNodeP, updated with new nodes for coercions
        void apply_equality(const Span& sp, const ::HIR::TypeRef& left, const ::HIR::TypeRef& right, ::HIR::ExprNodeP* node_ptr_ptr = nullptr)
        {
            apply_equality(sp, left, [](const auto& x)->const auto&{return x;}, right, [](const auto& x)->const auto&{return x;}, node_ptr_ptr);
        }
        
        void apply_equality(const Span& sp, const ::HIR::TypeRef& left, t_cb_generic cb_left, const ::HIR::TypeRef& right, t_cb_generic cb_right, ::HIR::ExprNodeP* node_ptr_ptr)
        {
            TRACE_FUNCTION_F(left << ", " << right);
            assert( ! left.m_data.is_Infer() ||  left.m_data.as_Infer().index != ~0u );
            assert( !right.m_data.is_Infer() || right.m_data.as_Infer().index != ~0u );
            // - Convert left/right types into resolved versions (either root ivar, or generic replacement)
            const auto& l_t = left.m_data.is_Generic()  ? cb_left (left ) : this->get_type(left );
            const auto& r_t = right.m_data.is_Generic() ? cb_right(right) : this->get_type(right);
            if( l_t == r_t ) {
                return ;
            }
            // If generic replacement happened, clear the callback
            if( left.m_data.is_Generic() ) {
                cb_left = [](const auto& x)->const auto&{return x;};
            }
            if( right.m_data.is_Generic() ) {
                cb_right = [](const auto& x)->const auto&{return x;};
            }
            DEBUG("- l_t = " << l_t << ", r_t = " << r_t);
            TU_IFLET(::HIR::TypeRef::Data, r_t.m_data, Infer, r_e,
                TU_IFLET(::HIR::TypeRef::Data, l_t.m_data, Infer, l_e,
                    // If both are infer, unify the two ivars (alias right to point to left)
                    this->ivar_unify(l_e.index, r_e.index);
                )
                else {
                    // Righthand side is infer, alias it to the left
                    //  TODO: that `true` should be `false` if the callback isn't unity (for bug checking)
                    this->set_ivar_to(r_e.index, monomorphise_type_with(sp, left, cb_left, true));
                }
            )
            else {
                TU_IFLET(::HIR::TypeRef::Data, l_t.m_data, Infer, l_e,
                    // Lefthand side is infer, alias it to the right
                    //  TODO: that `true` should be `false` if the callback isn't unity (for bug checking)
                    this->set_ivar_to(l_e.index, monomorphise_type_with(sp, right, cb_right, true));
                )
                else {
                    // Neither are infer - both should be of the same form
                    // - If either side is `!`, return early (diverging type, matches anything)
                    if( l_t.m_data.is_Diverge() || r_t.m_data.is_Diverge() ) {
                        // TODO: Should diverge check be done elsewhere? what happens if a ! ends up in an ivar?
                        return ;
                    }
                    // - If tags don't match, error
                    if( l_t.m_data.tag() != r_t.m_data.tag() ) {
                        // Type error
                        this->dump();
                        ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t);
                    }
                    TU_MATCH(::HIR::TypeRef::Data, (l_t.m_data, r_t.m_data), (l_e, r_e),
                    (Infer,
                        throw "";
                        ),
                    (Diverge,
                        TODO(sp, "Handle !");
                        ),
                    (Primitive,
                        if( l_e != r_e ) {
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t);
                        }
                        ),
                    (Path,
                        auto equality_typeparams = [&](const ::HIR::PathParams& l, const ::HIR::PathParams& r) {
                                if( l.m_types.size() != r.m_types.size() ) {
                                    ERROR(sp, E0000, "Type mismatch in type params `" << l << "` and `" << r << "`");
                                }
                                for(unsigned int i = 0; i < l.m_types.size(); i ++)
                                {
                                    this->apply_equality(sp, l.m_types[i], cb_left, r.m_types[i], cb_right, nullptr);
                                }
                            };
                        if( l_e.path.m_data.tag() != r_e.path.m_data.tag() ) {
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t);
                        }
                        TU_MATCH(::HIR::Path::Data, (l_e.path.m_data, r_e.path.m_data), (lpe, rpe),
                        (Generic,
                            if( lpe.m_path != rpe.m_path ) {
                                ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t);
                            }
                            equality_typeparams(lpe.m_params, rpe.m_params);
                            ),
                        (UfcsInherent,
                            this->apply_equality(sp, *lpe.type, cb_left, *rpe.type, cb_right, nullptr);
                            equality_typeparams(lpe.params, rpe.params);
                            ),
                        (UfcsKnown,
                            this->apply_equality(sp, *lpe.type, cb_left, *rpe.type, cb_right, nullptr);
                            equality_typeparams(lpe.trait.m_params, rpe.trait.m_params);
                            equality_typeparams(lpe.params, rpe.params);
                            ),
                        (UfcsUnknown,
                            this->apply_equality(sp, *lpe.type, cb_left, *rpe.type, cb_right, nullptr);
                            // TODO: If the type is fully known, locate a suitable trait item
                            equality_typeparams(lpe.params, rpe.params);
                            )
                        )
                        ),
                    (Generic,
                        if( l_e.binding != r_e.binding ) {
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t);
                        }
                        ),
                    (TraitObject,
                        if( l_e.m_traits.size() != r_e.m_traits.size() ) {
                            // TODO: Possibly allow inferrence reducing the set?
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t << " - trait counts differ");
                        }
                        // NOTE: Lifetime is ignored
                        // TODO: Is this list sorted for consistency?
                        for(unsigned int i = 0; i < l_e.m_traits.size(); i ++ )
                        {
                            auto& l_p = l_e.m_traits[i];
                            auto& r_p = r_e.m_traits[i];
                            if( l_p.m_path != r_p.m_path ) {
                                ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t);
                            }
                            // TODO: Equality of params
                        }
                        ),
                    (Array,
                        this->apply_equality(sp, *l_e.inner, cb_left, *r_e.inner, cb_right, nullptr);
                        if( l_e.size_val != r_e.size_val ) {
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t << " - sizes differ");
                        }
                        ),
                    (Slice,
                        this->apply_equality(sp, *l_e.inner, cb_left, *r_e.inner, cb_right, nullptr);
                        ),
                    (Tuple,
                        if( l_e.size() != r_e.size() ) {
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t << " - Tuples are of different length");
                        }
                        for(unsigned int i = 0; i < l_e.size(); i ++)
                        {
                            this->apply_equality(sp, l_e[i], cb_left, r_e[i], cb_right, nullptr);
                        }
                        ),
                    (Borrow,
                        if( l_e.type != r_e.type ) {
                            // TODO: This could be allowed if left == Shared && right == Unique (reborrowing)
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t << " - Borrow classes differ");
                        }
                        // ------------------
                        // Coercions!
                        // ------------------
                        if( node_ptr_ptr != nullptr )
                        {
                            const auto& left_inner_res  = this->get_type(*l_e.inner);
                            const auto& right_inner_res = this->get_type(*r_e.inner);
                            
                            // Allow cases where `right`: ::core::marker::Unsize<`left`>
                            bool succ = this->find_trait_impls(this->m_crate.get_lang_item_path(sp, "unsize"), right_inner_res, [&](const auto& args) {
                                DEBUG("- Found unsizing with args " << args);
                                return args.m_types[0] == left_inner_res;
                                });
                            if( succ ) {
                                auto span = (**node_ptr_ptr).span();
                                *node_ptr_ptr = ::HIR::ExprNodeP(new ::HIR::ExprNode_Unsize( mv$(span), mv$(*node_ptr_ptr), l_t.clone() ));
                                (*node_ptr_ptr)->m_res_type = l_t.clone();
                                
                                this->mark_change();
                                return ;
                            }
                            // - If left is a trait object, right can unsize
                            // - If left is a slice, right can unsize/deref
                            if( left_inner_res.m_data.is_Slice() && !right_inner_res.m_data.is_Slice() )
                            {
                                const auto& left_slice = left_inner_res.m_data.as_Slice();
                                TU_IFLET(::HIR::TypeRef::Data, right_inner_res.m_data, Array, right_array,
                                    this->apply_equality(sp, *left_slice.inner, cb_left, *right_array.inner, cb_right, nullptr);
                                    auto span = (**node_ptr_ptr).span();
                                    *node_ptr_ptr = ::HIR::ExprNodeP(new ::HIR::ExprNode_Unsize( mv$(span), mv$(*node_ptr_ptr), l_t.clone() ));
                                    (*node_ptr_ptr)->m_res_type = l_t.clone();
                                    
                                    this->mark_change();
                                    return ;
                                )
                                else TU_IFLET(::HIR::TypeRef::Data, right_inner_res.m_data, Generic, right_arg,
                                    TODO(sp, "Search for Unsize bound on generic");
                                )
                                else
                                {
                                    // Apply deref coercions
                                }
                            }
                            // - If right has a deref chain to left, build it
                        }
                        this->apply_equality(sp, *l_e.inner, cb_left, *r_e.inner, cb_right, nullptr);
                        ),
                    (Pointer,
                        if( l_e.is_mut != r_e.is_mut ) {
                            // TODO: This could be allowed if left == false && right == true (reborrowing)
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t << " - Pointer mutability differs");
                        }
                        this->apply_equality(sp, *l_e.inner, cb_left, *r_e.inner, cb_right, nullptr);
                        ),
                    (Function,
                        if( l_e.is_unsafe != r_e.is_unsafe
                            || l_e.m_abi != r_e.m_abi
                            || l_e.m_arg_types.size() != r_e.m_arg_types.size()
                            )
                        {
                            ERROR(sp, E0000, "Type mismatch between " << l_t << " and " << r_t);
                        }
                        // NOTE: No inferrence in fn types?
                        this->apply_equality(sp, *l_e.m_rettype, cb_left, *r_e.m_rettype, cb_right, nullptr);
                        for(unsigned int i = 0; i < l_e.m_arg_types.size(); i ++ ) {
                            this->apply_equality(sp, l_e.m_arg_types[i], cb_left, r_e.m_arg_types[i], cb_right, nullptr);
                        }
                        )
                    )
                }
            }
        }
        
        /// Searches for a trait impl that matches the provided trait name and type
        bool find_trait_impls(const ::HIR::SimplePath& trait, const ::HIR::TypeRef& type,  ::std::function<bool(const ::HIR::PathParams&)> callback)
        {
            TRACE_FUNCTION_F("trait = " << trait << ", type = " << type);
            // 1. Search generic params
            const ::HIR::GenericParams* v[2] = { m_item_params, m_impl_params };
            for(auto p : v)
            {
                if( !p )    continue ;
                for(const auto& b : p->m_bounds)
                {
                    TU_IFLET(::HIR::GenericBound, b, TraitBound, e,
                        DEBUG("Bound " << e.type << " : " << e.trait.m_path);
                        if( e.type == type && e.trait.m_path.m_path == trait ) {
                            if( callback(e.trait.m_path.m_params) ) {
                                return true;
                            }
                        }
                    )
                }
            }
            // 2. Search crate-level impls
            return find_trait_impls_crate(trait, type,  callback);
        }
        bool find_trait_impls_crate(const ::HIR::SimplePath& trait, const ::HIR::TypeRef& type,  ::std::function<bool(const ::HIR::PathParams&)> callback) const
        {
            auto its = m_crate.m_trait_impls.equal_range( trait );
            if( its.first != its.second )
            {
                for( auto it = its.first; it != its.second; ++ it )
                {
                    const auto& impl = it->second;
                    DEBUG("Compare " << type << " and " << impl.m_type);
                    if( impl.matches_type(type) ) {
                        if( callback(impl.m_trait_args) ) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }
        /// Locate the named method by applying auto-dereferencing.
        /// \return Number of times deref was applied (or ~0 if _ was hit)
        unsigned int autoderef_find_method(const Span& sp, const ::HIR::TypeRef& top_ty, const ::std::string method_name,  /* Out -> */::HIR::Path& fcn_path) const
        {
            unsigned int deref_count = 0;
            const auto* current_ty = &top_ty;
            do {
                const auto& ty = this->get_type(*current_ty);
                if( ty.m_data.is_Infer() ) {
                    return ~0u;
                }
                
                // 1. Search generic bounds for a match
                const ::HIR::GenericParams* v[2] = { m_item_params, m_impl_params };
                for(auto p : v)
                {
                    if( !p )    continue ;
                    for(const auto& b : p->m_bounds)
                    {
                        TU_IFLET(::HIR::GenericBound, b, TraitBound, e,
                            DEBUG("Bound " << e.type << " : " << e.trait.m_path);
                            // TODO: Match using _ replacement
                            if( e.type != ty )
                                continue ;
                            
                            // - Bound's type matches, check if the bounded trait has the method we're searching for
                            //  > TODO: Search supertraits too
                            DEBUG("- Matches " << ty);
                            assert(e.trait.m_trait_ptr);
                            auto it = e.trait.m_trait_ptr->m_values.find(method_name);
                            if( it == e.trait.m_trait_ptr->m_values.end() )
                                continue ;
                            if( !it->second.is_Function() )
                                continue ;
                            
                            // Found the method, return the UFCS path for it
                            fcn_path = ::HIR::Path( ::HIR::Path::Data::make_UfcsKnown({
                                box$( ty.clone() ),
                                e.trait.m_path.clone(),
                                method_name,
                                {}
                                }) );
                            return deref_count;
                        )
                    }
                }
                
                TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Generic, e,
                    // No match, keep trying.
                )
                else {
                    // 2. Search for inherent methods
                    for(const auto& impl : m_crate.m_type_impls)
                    {
                        if( impl.matches_type(ty) ) {
                            DEBUG("Mactching impl " << impl.m_type);
                            fcn_path = ::HIR::Path( ::HIR::Path::Data::make_UfcsInherent({
                                box$(ty.clone()),
                                method_name,
                                {}
                                }) );
                            return deref_count;
                        }
                    }
                    // 3. Search for trait methods (using currently in-scope traits)
                    for(const auto& trait_ref : ::reverse(m_traits))
                    {
                        auto it = trait_ref.second->m_values.find(method_name);
                        if( it == trait_ref.second->m_values.end() )
                            continue ;
                        if( !it->second.is_Function() )
                            continue ;
                        DEBUG("Search for impl of " << *trait_ref.first);
                        if( find_trait_impls_crate(*trait_ref.first, ty,  [](const auto&) { return true; }) ) {
                            DEBUG("Found trait impl " << *trait_ref.first << " for " << ty);
                            fcn_path = ::HIR::Path( ::HIR::Path::Data::make_UfcsKnown({
                                box$( ty.clone() ),
                                trait_ref.first->clone(),
                                method_name,
                                {}
                                }) );
                            return deref_count;
                        }
                    }
                }
                
                // 3. Dereference and try again
                deref_count += 1;
                TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Borrow, e,
                    current_ty = &*e.inner;
                )
                else {
                    // TODO: Search for a Deref impl
                    current_ty = nullptr;
                }
            } while( current_ty );
            // Dereference failed! This is a hard error (hitting _ is checked above and returns ~0)
            TODO(sp, "Error when no method could be found, but type is known - (: " << top_ty << ")." << method_name);
        }
        
    public:
        unsigned int new_ivar()
        {
            m_ivars.push_back( IVar() );
            m_ivars.back().type->m_data.as_Infer().index = m_ivars.size() - 1;
            return m_ivars.size() - 1;
        }
        void del_ivar(unsigned int index)
        {
            DEBUG("Deleting ivar " << index << " of  " << m_ivars.size());
            if( index == m_ivars.size() - 1 ) {
                m_ivars.pop_back();
            }
            else {
                assert(!"Can't delete an ivar after it's been used");
                m_ivars[index].deleted = true;
            }
        }
        ::HIR::TypeRef new_ivar_tr() {
            ::HIR::TypeRef rv;
            rv.m_data.as_Infer().index = this->new_ivar();
            return rv;
        }
        
        IVar& get_pointed_ivar(unsigned int slot) const
        {
            auto index = slot;
            unsigned int count = 0;
            assert(index < m_ivars.size());
            while( m_ivars.at(index).is_alias() ) {
                assert( m_ivars.at(index).deleted == false );
                index = m_ivars.at(index).alias;
                
                if( count >= m_ivars.size() ) {
                    this->dump();
                    BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
                }
                count ++;
            }
            assert( m_ivars.at(index).deleted == false );
            return const_cast<IVar&>(m_ivars.at(index));
        }
        ::HIR::TypeRef& get_type(::HIR::TypeRef& type)
        {
            TU_IFLET(::HIR::TypeRef::Data, type.m_data, Infer, e,
                assert(e.index != ~0u);
                return *get_pointed_ivar(e.index).type;
            )
            else {
                return type;
            }
        }
        const ::HIR::TypeRef& get_type(const ::HIR::TypeRef& type) const
        {
            TU_IFLET(::HIR::TypeRef::Data, type.m_data, Infer, e,
                assert(e.index != ~0u);
                return *get_pointed_ivar(e.index).type;
            )
            else {
                return type;
            }
        }
        
        void set_ivar_to(unsigned int slot, ::HIR::TypeRef type)
        {
            auto& root_ivar = this->get_pointed_ivar(slot);
            
            // If the left type wasn't a reference to an ivar, store it in the righthand ivar
            TU_IFLET(::HIR::TypeRef::Data, type.m_data, Infer, l_e,
                assert( l_e.index != slot );
                DEBUG("Set IVar " << slot << " = @" << l_e.index);
                root_ivar.alias = l_e.index;
                root_ivar.type.reset();
            )
            else {
                DEBUG("Set IVar " << slot << " = " << type);
                root_ivar.type = box$( mv$(type) );
            }
            
            this->mark_change();
        }

        void ivar_unify(unsigned int left_slot, unsigned int right_slot)
        {
            if( left_slot != right_slot )
            {
                // TODO: Assert that setting this won't cause a loop.
                auto& root_ivar = this->get_pointed_ivar(right_slot);
                root_ivar.alias = left_slot;
                root_ivar.type.reset();
                
                this->mark_change();
            }
        }
    };
    
    // Enumerate inferrence variables (most of them) in the expression tree
    //
    // - Any type equalities here are mostly optimisations (as this gets run only once)
    //  - If ivars can be shared down the tree - good.
    class ExprVisitor_Enum:
        public ::HIR::ExprVisitorDef
    {
        TypecheckContext& context;
        const ::HIR::TypeRef&   ret_type;
    public:
        ExprVisitor_Enum(TypecheckContext& context, const ::HIR::TypeRef& ret_type):
            context(context),
            ret_type(ret_type)
        {
        }
        
        void visit_node(::HIR::ExprNode& node) override {
            this->context.add_ivars(node.m_res_type);
            DEBUG(typeid(node).name() << " : " << node.m_res_type);
        }
        void visit(::HIR::ExprNode_Block& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            if( node.m_nodes.size() > 0 ) {
                auto& ln = *node.m_nodes.back();
                // If the child node didn't set a real return type, force it to be the same as this node's
                if( ln.m_res_type.m_data.is_Infer() ) {
                    ln.m_res_type = node.m_res_type.clone();
                }
                else {
                    // If it was set, equate with possiblity of coercion
                    this->context.apply_equality(ln.span(), node.m_res_type, ln.m_res_type, &node.m_nodes.back());
                }
            }
            else {
                node.m_res_type = ::HIR::TypeRef::new_unit();
            }
        }
        void visit(::HIR::ExprNode_Return& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            this->context.apply_equality(node.span(), this->ret_type, node.m_value->m_res_type,  &node.m_value);
        }
        
        void visit(::HIR::ExprNode_Let& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            TRACE_FUNCTION_F("let " << node.m_pattern << ": " << node.m_type);
            
            this->context.add_ivars(node.m_type);
            
            this->context.add_binding(node.span(), node.m_pattern, node.m_type);
        }
        
        void visit(::HIR::ExprNode_Match& node) override
        {
            TRACE_FUNCTION_F("match ...");
            
            this->context.add_ivars(node.m_value->m_res_type);
            
            for(auto& arm : node.m_arms)
            {
                DEBUG("ARM " << arm.m_patterns);
                for(auto& pat : arm.m_patterns)
                {
                    this->context.add_binding(node.span(), pat, node.m_value->m_res_type);
                }
            }

            ::HIR::ExprVisitorDef::visit(node);
        }
        void visit(::HIR::ExprNode_If& node) override
        {
            node.m_cond->m_res_type = ::HIR::TypeRef( ::HIR::CoreType::Bool );
            if( node.m_false ) {
                node.m_true->m_res_type = node.m_res_type.clone();
                node.m_false->m_res_type = node.m_res_type.clone();
            }
            else {
                this->context.apply_equality(node.span(), node.m_res_type, ::HIR::TypeRef::new_unit());
                node.m_true->m_res_type = node.m_res_type.clone();
            }
            
            ::HIR::ExprVisitorDef::visit(node);
        }

        void visit(::HIR::ExprNode_UniOp& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            
            switch(node.m_op)
            {
            case ::HIR::ExprNode_UniOp::Op::Ref:
                this->context.apply_equality(node.span(), node.m_res_type, ::HIR::TypeRef::new_borrow(::HIR::BorrowType::Shared, node.m_value->m_res_type.clone()));
                break;
            case ::HIR::ExprNode_UniOp::Op::RefMut:
                this->context.apply_equality(node.span(), node.m_res_type, ::HIR::TypeRef::new_borrow(::HIR::BorrowType::Unique, node.m_value->m_res_type.clone()));
                break;
            case ::HIR::ExprNode_UniOp::Op::Invert:
                break;
            case ::HIR::ExprNode_UniOp::Op::Negate:
                break;
            }
        }

        void visit(::HIR::ExprNode_Tuple& node) override
        {
            // Only delete and apply if the return type is an ivar
            // - Can happen with `match (a, b)`
            TU_IFLET(::HIR::TypeRef::Data, node.m_res_type.m_data, Infer, e,
                // - Remove the ivar created by the generic visitor
                this->context.dump();
                this->context.del_ivar( e.index );
            )

            ::HIR::ExprVisitorDef::visit(node);

            if( node.m_res_type.m_data.is_Infer() )
            {
                ::std::vector< ::HIR::TypeRef>  types;
                for( const auto& sn : node.m_vals )
                    types.push_back( sn->m_res_type.clone() );
                node.m_res_type = ::HIR::TypeRef( ::HIR::TypeRef::Data::make_Tuple(mv$(types)) );
            }
        }
        void visit(::HIR::ExprNode_Closure& node) override
        {
            for(auto& a : node.m_args) {
                this->context.add_ivars(a.second);
                this->context.add_binding(node.span(), a.first, a.second);
            }
            this->context.add_ivars(node.m_return);
            node.m_code->m_res_type = node.m_return.clone();

            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Variable: Bind to same ivar
        void visit(::HIR::ExprNode_Variable& node) override
        {
            TU_IFLET(::HIR::TypeRef::Data, node.m_res_type.m_data, Infer, e,
                this->context.del_ivar( e.index );
            )
            node.m_res_type = this->context.get_var_type(node.span(), node.m_slot).clone();
        }
        
        void visit(::HIR::ExprNode_CallPath& node) override
        {
            TU_MATCH(::HIR::Path::Data, (node.m_path.m_data), (e),
            (Generic,
                for(auto& ty : e.m_params.m_types)
                    this->context.add_ivars(ty);
                ),
            (UfcsKnown,
                this->context.add_ivars(*e.type);
                for(auto& ty : e.trait.m_params.m_types)
                    this->context.add_ivars(ty);
                for(auto& ty : e.params.m_types)
                    this->context.add_ivars(ty);
                ),
            (UfcsUnknown,
                TODO(node.span(), "Hit a UfcsUnknown (" << node.m_path << ") - Is this an error?");
                ),
            (UfcsInherent,
                this->context.add_ivars(*e.type);
                for(auto& ty : e.params.m_types)
                    this->context.add_ivars(ty);
                )
            )
            ::HIR::ExprVisitorDef::visit(node);
        }
    };
    
    // Continually run over the expression tree until nothing changes
    class ExprVisitor_Run:
        public ::HIR::ExprVisitorDef
    {
        TypecheckContext& context;
    public:
        ExprVisitor_Run(TypecheckContext& context):
            context(context)
        {
        }
        
        // - Block: Ignore all return values except the last one (which is yeilded)
        void visit(::HIR::ExprNode_Block& node) override
        {
            TRACE_FUNCTION_F("{ }");
            if( node.m_nodes.size() ) {
                auto& lastnode = node.m_nodes.back();
                this->context.apply_equality(node.span(), node.m_res_type, lastnode->m_res_type,  &lastnode);
            }
            else {
                this->context.apply_equality(node.span(), node.m_res_type, ::HIR::TypeRef::new_unit());
            }
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Let: Equates inner to outer
        void visit(::HIR::ExprNode_Let& node) override
        {
            TRACE_FUNCTION_F("let " << node.m_pattern << " : " << node.m_type);
            if( node.m_value ) {
                this->context.apply_equality(node.span(), node.m_type, node.m_value->m_res_type,  &node.m_value);
            }

            ::HIR::ExprVisitorDef::visit(node);
        }
        
        // - If: Both branches have to agree
        void visit(::HIR::ExprNode_If& node) override
        {
            TRACE_FUNCTION_F("if ...");
            this->context.apply_equality(node.span(), node.m_res_type, node.m_true->m_res_type,  &node.m_true);
            if( node.m_false ) {
                this->context.apply_equality(node.span(), node.m_res_type, node.m_false->m_res_type, &node.m_false);
            }
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Match: all branches match
        void visit(::HIR::ExprNode_Match& node) override
        {
            TRACE_FUNCTION_F("match ...");
            
            for(auto& arm : node.m_arms)
            {
                DEBUG("ARM " << arm.m_patterns);
                // TODO: Span on the arm
                this->context.apply_equality(node.span(), node.m_res_type, arm.m_code->m_res_type, &arm.m_code);
            }
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Assign: both sides equal
        void visit(::HIR::ExprNode_Assign& node) override
        {
            TRACE_FUNCTION_F("... = ...");
            if( node.m_op == ::HIR::ExprNode_Assign::Op::None ) {
                this->context.apply_equality(node.span(),
                    node.m_slot->m_res_type, node.m_value->m_res_type,
                    &node.m_value
                    );
            }
            else {
                // TODO: Look for overload
            }
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - BinOp: Look for overload or primitive
        void visit(::HIR::ExprNode_BinOp& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            const auto& ty_left  = this->context.get_type(node.m_left->m_res_type );
            const auto& ty_right = this->context.get_type(node.m_right->m_res_type);
            
            if( ty_left.m_data.is_Primitive() && ty_right.m_data.is_Primitive() ) 
            {
                const auto& prim_left  = ty_left.m_data.as_Primitive();
                const auto& prim_right = ty_right.m_data.as_Primitive();
                switch(node.m_op)
                {
                case ::HIR::ExprNode_BinOp::Op::CmpEqu:
                case ::HIR::ExprNode_BinOp::Op::CmpNEqu:
                case ::HIR::ExprNode_BinOp::Op::CmpLt:
                case ::HIR::ExprNode_BinOp::Op::CmpLtE:
                case ::HIR::ExprNode_BinOp::Op::CmpGt:
                case ::HIR::ExprNode_BinOp::Op::CmpGtE:
                    if( prim_left != prim_right ) {
                        ERROR(node.span(), E0000, "Mismatched types in comparison");
                    }
                    break;
               
                case ::HIR::ExprNode_BinOp::Op::BoolAnd:
                case ::HIR::ExprNode_BinOp::Op::BoolOr:
                    if( prim_left != ::HIR::CoreType::Bool || prim_right != ::HIR::CoreType::Bool ) {
                        ERROR(node.span(), E0000, "Use of non-boolean in boolean and/or");
                    }
                    break;

                case ::HIR::ExprNode_BinOp::Op::Add:
                case ::HIR::ExprNode_BinOp::Op::Sub:
                case ::HIR::ExprNode_BinOp::Op::Mul:
                case ::HIR::ExprNode_BinOp::Op::Div:
                case ::HIR::ExprNode_BinOp::Op::Mod:
                    if( prim_left != prim_right ) {
                        ERROR(node.span(), E0000, "Mismatched types in arithmatic operation");
                    }
                    switch(prim_left)
                    {
                    case ::HIR::CoreType::Str:
                    case ::HIR::CoreType::Char:
                    case ::HIR::CoreType::Bool:
                        ERROR(node.span(), E0000, "Invalid use of arithmatic on " << ty_left);
                        break;
                    default:
                        this->context.apply_equality(node.span(), node.m_res_type, ty_left);
                    }
                    break;
                case ::HIR::ExprNode_BinOp::Op::And:
                case ::HIR::ExprNode_BinOp::Op::Or:
                case ::HIR::ExprNode_BinOp::Op::Xor:
                    if( prim_left != prim_right ) {
                        ERROR(node.span(), E0000, "Mismatched types in bitwise operation");
                    }
                    switch(prim_left)
                    {
                    case ::HIR::CoreType::Str:
                    case ::HIR::CoreType::Char:
                    case ::HIR::CoreType::Bool:
                    case ::HIR::CoreType::F32:
                    case ::HIR::CoreType::F64:
                        ERROR(node.span(), E0000, "Invalid use of bitwise operation on " << ty_left);
                        break;
                    default:
                        this->context.apply_equality(node.span(), node.m_res_type, ty_left);
                    }
                    break;
                case ::HIR::ExprNode_BinOp::Op::Shr:
                case ::HIR::ExprNode_BinOp::Op::Shl:
                    switch(prim_left)
                    {
                    case ::HIR::CoreType::Str:
                    case ::HIR::CoreType::Char:
                    case ::HIR::CoreType::Bool:
                    case ::HIR::CoreType::F32:
                    case ::HIR::CoreType::F64:
                        ERROR(node.span(), E0000, "Invalid type for shift count - " << ty_right);
                    default:
                        break;
                    }
                    switch(prim_left)
                    {
                    case ::HIR::CoreType::Str:
                    case ::HIR::CoreType::Char:
                    case ::HIR::CoreType::Bool:
                    case ::HIR::CoreType::F32:
                    case ::HIR::CoreType::F64:
                        ERROR(node.span(), E0000, "Invalid use of shift on " << ty_left);
                        break;
                    default:
                        this->context.apply_equality(node.span(), node.m_res_type, ty_left);
                    }
                    break;
                }
            }
            else
            {
                // TODO: Search for ops trait impl
            }
        }
        // - UniOp: Look for overload or primitive
        void visit(::HIR::ExprNode_UniOp& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            
            const auto& ty = this->context.get_type(node.m_value->m_res_type);
            switch(node.m_op)
            {
            case ::HIR::ExprNode_UniOp::Op::Ref:
                // - Handled above?
                break;
            case ::HIR::ExprNode_UniOp::Op::RefMut:
                // - Handled above?
                break;
            case ::HIR::ExprNode_UniOp::Op::Invert:
                TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Primitive, e,
                    switch(e)
                    {
                    case ::HIR::CoreType::Str:
                    case ::HIR::CoreType::Char:
                    case ::HIR::CoreType::F32:
                    case ::HIR::CoreType::F64:
                        ERROR(node.span(), E0000, "Invalid use of ! on " << ty);
                        break;
                    default:
                        this->context.apply_equality(node.span(), node.m_res_type, ty);
                        break;
                    }
                )
                else {
                    // TODO: Search for an implementation of ops::Not
                }
                break;
            case ::HIR::ExprNode_UniOp::Op::Negate:
                TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Primitive, e,
                    switch(e)
                    {
                    case ::HIR::CoreType::Str:
                    case ::HIR::CoreType::Char:
                    case ::HIR::CoreType::Bool:
                        ERROR(node.span(), E0000, "Invalid use of - on " << ty);
                        break;
                    default:
                        this->context.apply_equality(node.span(), node.m_res_type, ty);
                        break;
                    }
                )
                else {
                    // TODO: Search for an implementation of ops::Neg
                }
                break;
            }
        }
        // - Cast: Nothing needs to happen
        void visit(::HIR::ExprNode_Cast& node) override
        {
            // TODO: Check cast validity?
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Index: Look for implementation of the Index trait
        void visit(::HIR::ExprNode_Index& node) override
        {
            this->context.find_trait_impls(this->context.m_crate.get_lang_item_path(node.span(), "index"), node.m_val->m_res_type, [&](const auto& args) {
                DEBUG("TODO: Insert index operator (if index arg matches)");
                return false;
                });
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Deref: Look for impl of Deref
        void visit(::HIR::ExprNode_Deref& node) override
        {
            const auto& ty = this->context.get_type( node.m_val->m_res_type );
            TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Borrow, e,
                this->context.apply_equality(node.span(), node.m_res_type, *e.inner);
            )
            else {
                // TODO: Search for Deref impl
            }
            ::HIR::ExprVisitorDef::visit(node);
        }
        
        void fix_param_count(const Span& sp, const ::HIR::Path& path, const ::HIR::GenericParams& param_defs,  ::HIR::PathParams& params)
        {
            if( params.m_types.size() == param_defs.m_types.size() ) {
                // Nothing to do, all good
                return ;
            }
            
            if( params.m_types.size() == 0 ) {
                for(const auto& typ : param_defs.m_types) {
                    (void)typ;
                    params.m_types.push_back( this->context.new_ivar_tr() );
                }
            }
            else if( params.m_types.size() > param_defs.m_types.size() ) {
                ERROR(sp, E0000, "Too many type parameters passed to " << path);
            }
            else {
                while( params.m_types.size() < param_defs.m_types.size() ) {
                    const auto& typ = param_defs.m_types[params.m_types.size()];
                    if( typ.m_default.m_data.is_Infer() ) {
                        ERROR(sp, E0000, "Omitted type parameter with no default in " << path);
                    }
                    else {
                        // TODO: What if this contains a generic param? (is that valid?)
                        params.m_types.push_back( typ.m_default.clone() );
                    }
                }
            }
        }
        
        void visit_call(const Span& sp, ::HIR::Path& path, bool is_method, ::std::vector< ::HIR::ExprNodeP>& args, ::HIR::TypeRef& res_type)
        {
            const ::HIR::Function*  fcn_ptr = nullptr;
            ::std::function<const ::HIR::TypeRef&(const ::HIR::TypeRef&)>    monomorph_cb;
            
            TRACE_FUNCTION_F("path = " << path);
            unsigned int arg_ofs = (is_method ? 1 : 0);
            // TODO: Construct method to get a reference to an item along with the params decoded out of the pat
            TU_MATCH(::HIR::Path::Data, (path.m_data), (e),
            (Generic,
                const auto& fcn = this->context.m_crate.get_function_by_path(sp, e.m_path);
                this->fix_param_count(sp, path, fcn.m_params,  e.m_params);
                fcn_ptr = &fcn;
                
                //const auto& params_def = fcn.m_params;
                const auto& path_params = e.m_params;
                monomorph_cb = [&](const auto& gt)->const auto& {
                        const auto& e = gt.m_data.as_Generic();
                        if( e.name == "Self" )
                            TODO(sp, "Handle 'Self' when monomorphising");
                        //if( e.binding >= params_def.m_types.size() ) {
                        //}
                        if( e.binding >= path_params.m_types.size() ) {
                            BUG(sp, "Generic param out of input range - " << e.binding << " '"<<e.name<<"' >= " << path_params.m_types.size());
                        }
                        return path_params.m_types[e.binding];
                    };
                ),
            (UfcsKnown,
                const auto& trait = this->context.m_crate.get_trait_by_path(sp, e.trait.m_path);
                this->fix_param_count(sp, path, trait.m_params, e.trait.m_params);
                const auto& fcn = trait.m_values.find(e.item)->second.as_Function();
                this->fix_param_count(sp, path, fcn.m_params,  e.params);
                
                fcn_ptr = &fcn;
                
                const auto& path_params = e.params;
                monomorph_cb = [&](const auto& gt)->const auto& {
                        const auto& ge = gt.m_data.as_Generic();
                        if( ge.binding == 0xFFFF ) {
                            return *e.type;
                        }
                        // TODO: Don't the function-level params use 256-511?
                        else if( ge.binding < 256 ) {
                            return path_params.m_types[ge.binding];
                        }
                        else {
                        }
                        TODO(sp, "Monomorphise for trait method");
                    };
                ),
            (UfcsUnknown,
                TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                ),
            (UfcsInherent,
                TODO(sp, "Locate functions in UFCS inherent - " << path);
                )
            )

            assert( fcn_ptr );
            const auto& fcn = *fcn_ptr;
            
            if( args.size() + (is_method ? 1 : 0) != fcn.m_args.size() ) {
                ERROR(sp, E0000, "Incorrect number of arguments to " << path);
            }
            
            // TODO: Avoid needing to monomorphise here
            // - Have two callbacks to apply_equality that are used to expand `Generic`s (cleared once used)
            for( unsigned int i = arg_ofs; i < fcn.m_args.size(); i ++ )
            {
                auto& arg_expr_ptr = args[i - arg_ofs];
                const auto& arg_ty = fcn.m_args[i].second;
                DEBUG("Arg " << i << ": " << arg_ty);
                ::HIR::TypeRef  mono_type;
                const auto& ty = (monomorphise_type_needed(arg_ty) ? (mono_type = monomorphise_type_with(sp, arg_ty,  monomorph_cb)) : arg_ty);
                this->context.apply_equality(sp, ty, arg_expr_ptr->m_res_type,  &arg_expr_ptr);
            }
            DEBUG("RV " << fcn.m_return);
            ::HIR::TypeRef  mono_type;
            const auto& ty = (monomorphise_type_needed(fcn.m_return) ? (mono_type = monomorphise_type_with(sp, fcn.m_return,  monomorph_cb)) : fcn.m_return);
            this->context.apply_equality(sp, res_type, ty);
        }
        
        // - Call Path: Locate path and build return
        void visit(::HIR::ExprNode_CallPath& node) override
        {
            TRACE_FUNCTION_F("CallPath " << node.m_path);
            visit_call(node.span(), node.m_path, false, node.m_args, node.m_res_type);
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Call Value: If type is known, locate impl of Fn/FnMut/FnOnce
        void visit(::HIR::ExprNode_CallValue& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Call Method: Locate method on type
        void visit(::HIR::ExprNode_CallMethod& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            if( node.m_method_path.m_data.is_Generic() && node.m_method_path.m_data.as_Generic().m_path.m_components.size() == 0 )
            {
                const auto& ty = this->context.get_type(node.m_val->m_res_type);
                DEBUG("ty = " << ty);
                // Using autoderef, locate this method on the type
                ::HIR::Path   fcn_path { ::HIR::SimplePath() };
                unsigned int deref_count = this->context.autoderef_find_method(node.span(), ty, node.m_method,  fcn_path);
                if( deref_count != ~0u )
                {
                    DEBUG("Found method " << fcn_path);
                    node.m_method_path = mv$(fcn_path);
                    // NOTE: Steals the params from the node
                    TU_MATCH(::HIR::Path::Data, (node.m_method_path.m_data), (e),
                    (Generic,
                        ),
                    (UfcsUnknown,
                        ),
                    (UfcsKnown,
                        e.params = mv$(node.m_params);
                        ),
                    (UfcsInherent,
                        e.params = mv$(node.m_params);
                        )
                    )
                    DEBUG("Adding " << deref_count << " dereferences");
                    while( deref_count > 0 )
                    {
                        node.m_val = ::HIR::ExprNodeP( new ::HIR::ExprNode_Deref(node.span(), mv$(node.m_val)) );
                        this->context.add_ivars( node.m_val->m_res_type );
                        deref_count -= 1;
                    }
                }
            }
            
            // TODO: Look up method based on node.m_method_path, using shared code with ExprNode_CallPath
            visit_call(node.span(), node.m_method_path, true, node.m_args, node.m_res_type);
        }
        // - Field: Locate field on type
        void visit(::HIR::ExprNode_Field& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - PathValue: Insert type from path
        void visit(::HIR::ExprNode_PathValue& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Variable: Bind to same ivar
        void visit(::HIR::ExprNode_Variable& node) override
        {
            // TODO: How to apply deref coercions here?
            // - Don't need to, instead construct "higher" nodes to avoid it
            TRACE_FUNCTION_F("var #"<<node.m_slot<<" '"<<node.m_name<<"'");
            this->context.apply_equality(node.span(),
                node.m_res_type, this->context.get_var_type(node.span(), node.m_slot)
                );
        }
        // - Struct literal: Semi-known types
        void visit(::HIR::ExprNode_StructLiteral& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Tuple literal: 
        void visit(::HIR::ExprNode_Tuple& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
        }
        // - Array list
        void visit(::HIR::ExprNode_ArrayList& node) override
        {
            const auto& val_type = *node.m_res_type.m_data.as_Array().inner;
            ::HIR::ExprVisitorDef::visit(node);
            for(auto& sn : node.m_vals)
                this->context.apply_equality(sn->span(), val_type, sn->m_res_type,  &sn);
        }
        // - Array (sized)
        void visit(::HIR::ExprNode_ArraySized& node) override
        {
            const auto& val_type = *node.m_res_type.m_data.as_Array().inner;
            ::HIR::ExprVisitorDef::visit(node);
            this->context.apply_equality(node.span(), val_type, node.m_val->m_res_type,  &node.m_val);
        }
        // - Closure
        void visit(::HIR::ExprNode_Closure& node) override
        {
            ::HIR::ExprVisitorDef::visit(node);
            this->context.apply_equality(node.span(), node.m_return, node.m_code->m_res_type,  &node.m_code);
        }
    };
    
    /// Visitor that applies the inferred types, and checks that all of them are fully resolved
    class ExprVisitor_Apply:
        public ::HIR::ExprVisitorDef
    {
        TypecheckContext& context;
    public:
        ExprVisitor_Apply(TypecheckContext& context):
            context(context)
        {
        }
        void visit_node(::HIR::ExprNode& node) override {
            DEBUG(typeid(node).name() << " : " << node.m_res_type);
            this->check_type_resolved(node.span(), node.m_res_type, node.m_res_type);
            DEBUG(typeid(node).name() << " : = " << node.m_res_type);
        }
        
    private:
        void check_type_resolved(const Span& sp, ::HIR::TypeRef& ty, const ::HIR::TypeRef& top_type) const {
            TU_MATCH(::HIR::TypeRef::Data, (ty.m_data), (e),
            (Infer,
                auto new_ty = this->context.get_type(ty).clone();
                if( new_ty.m_data.is_Infer() ) {
                    ERROR(sp, E0000, "Failed to infer type " << top_type);
                }
                ty = mv$(new_ty);
                ),
            (Diverge,
                // Leaf
                ),
            (Primitive,
                // Leaf
                ),
            (Path,
                // TODO:
                ),
            (Generic,
                // Leaf
                ),
            (TraitObject,
                // TODO:
                ),
            (Array,
                this->check_type_resolved(sp, *e.inner, top_type);
                ),
            (Slice,
                this->check_type_resolved(sp, *e.inner, top_type);
                ),
            (Tuple,
                for(auto& st : e)
                    this->check_type_resolved(sp, st, top_type);
                ),
            (Borrow,
                this->check_type_resolved(sp, *e.inner, top_type);
                ),
            (Pointer,
                this->check_type_resolved(sp, *e.inner, top_type);
                ),
            (Function,
                // TODO:
                )
            )
        }
    };
};

void Typecheck_Code(TypecheckContext context, const ::HIR::TypeRef& result_type, ::HIR::ExprPtr& expr)
{
    TRACE_FUNCTION;
    
    // TODO: Perform type propagation "outward" from the root
    
    //context.apply_equality(expr->span(), result_type, expr->m_res_type);

    // 1. Enumerate inferrence variables and assign indexes to them
    {
        ExprVisitor_Enum    visitor { context, result_type };
        expr->visit( visitor );
    }
    // - Apply equality between the node result and the expected type
    DEBUG("- Apply RV");
    {
        // Convert ExprPtr into unique_ptr for the execution of this function
        auto root_ptr = expr.into_unique();
        context.apply_equality(root_ptr->span(), result_type, root_ptr->m_res_type,  &root_ptr);
        expr = ::HIR::ExprPtr( mv$(root_ptr) );
    }
    
    context.dump();
    // 2. Iterate through nodes applying rules until nothing changes
    {
        ExprVisitor_Run visitor { context };
        do {
            expr->visit( visitor );
        } while( context.take_changed() );
    }
    
    // 3. Check that there's no unresolved types left
    // TODO: Check for completed type resolution
    context.dump();
    {
        ExprVisitor_Apply   visitor { context };
        expr->visit( visitor );
    }
}



namespace {
    class OuterVisitor:
        public ::HIR::Visitor
    {
        ::HIR::Crate& m_crate;
        
        ::HIR::GenericParams*   m_impl_generics;
        ::HIR::GenericParams*   m_item_generics;
        ::std::vector< ::std::pair< const ::HIR::SimplePath*, const ::HIR::Trait* > >   m_traits;
    public:
        OuterVisitor(::HIR::Crate& crate):
            m_crate(crate),
            m_impl_generics(nullptr),
            m_item_generics(nullptr)
        {
        }
        
    private:
        template<typename T>
        class NullOnDrop {
            T*& ptr;
        public:
            NullOnDrop(T*& ptr):
                ptr(ptr)
            {}
            ~NullOnDrop() {
                ptr = nullptr;
            }
        };
        NullOnDrop< ::HIR::GenericParams> set_impl_generics(::HIR::GenericParams& gps) {
            assert( !m_impl_generics );
            m_impl_generics = &gps;
            return NullOnDrop< ::HIR::GenericParams>(m_impl_generics);
        }
        NullOnDrop< ::HIR::GenericParams> set_item_generics(::HIR::GenericParams& gps) {
            assert( !m_item_generics );
            m_item_generics = &gps;
            return NullOnDrop< ::HIR::GenericParams>(m_item_generics);
        }
    
    public:
        void visit_module(::HIR::PathChain p, ::HIR::Module& mod) override
        {
            DEBUG("Module has " << mod.m_traits.size() << " in-scope traits");
            for( const auto& trait_path : mod.m_traits ) {
                DEBUG("Push " << trait_path);
                m_traits.push_back( ::std::make_pair( &trait_path, &this->m_crate.get_trait_by_path(Span(), trait_path) ) );
            }
            ::HIR::Visitor::visit_module(p, mod);
            for(unsigned int i = 0; i < mod.m_traits.size(); i ++ )
                m_traits.pop_back();
        }
        
        // NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
        void visit_expr(::HIR::ExprPtr& exp) {
            TODO(Span(), "visit_expr");
        }

        void visit_trait(::HIR::PathChain p, ::HIR::Trait& item) override
        {
            auto _ = this->set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_trait(p, item);
        }
        
        void visit_type_impl(::HIR::TypeImpl& impl) override
        {
            TRACE_FUNCTION_F("impl " << impl.m_type);
            auto _ = this->set_impl_generics(impl.m_params);
            
            ::HIR::Visitor::visit_type_impl(impl);
        }
        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override
        {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.m_type);
            auto _ = this->set_impl_generics(impl.m_params);
            
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        }
        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override
        {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.m_type << " { }");
            auto _ = this->set_impl_generics(impl.m_params);
            
            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
        }
        
        void visit_type(::HIR::TypeRef& ty) override
        {
            TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Array, e,
                this->visit_type( *e.inner );
                TypecheckContext    typeck_context { m_crate, m_impl_generics, m_item_generics };
                typeck_context.m_traits = this->m_traits;
                DEBUG("Array size " << ty);
                Typecheck_Code( mv$(typeck_context), ::HIR::TypeRef(::HIR::CoreType::Usize), e.size );
            )
            else {
                ::HIR::Visitor::visit_type(ty);
            }
        }
        // ------
        // Code-containing items
        // ------
        void visit_function(::HIR::PathChain p, ::HIR::Function& item) override {
            auto _ = this->set_item_generics(item.m_params);
            if( item.m_code )
            {
                TypecheckContext typeck_context { m_crate, m_impl_generics, m_item_generics };
                typeck_context.m_traits = this->m_traits;
                for( auto& arg : item.m_args ) {
                    typeck_context.add_binding( Span(), arg.first, arg.second );
                }
                DEBUG("Function code " << p);
                Typecheck_Code( mv$(typeck_context), item.m_return, item.m_code );
            }
        }
        void visit_static(::HIR::PathChain p, ::HIR::Static& item) override {
            //auto _ = this->set_item_generics(item.m_params);
            if( item.m_value )
            {
                TypecheckContext typeck_context { m_crate, m_impl_generics, m_item_generics };
                typeck_context.m_traits = this->m_traits;
                DEBUG("Static value " << p);
                Typecheck_Code( mv$(typeck_context), item.m_type, item.m_value );
            }
        }
        void visit_constant(::HIR::PathChain p, ::HIR::Constant& item) override {
            auto _ = this->set_item_generics(item.m_params);
            if( item.m_value )
            {
                TypecheckContext typeck_context { m_crate, m_impl_generics, m_item_generics };
                typeck_context.m_traits = this->m_traits;
                DEBUG("Const value " << p);
                Typecheck_Code( mv$(typeck_context), item.m_type, item.m_value );
            }
        }
        void visit_enum(::HIR::PathChain p, ::HIR::Enum& item) override {
            auto _ = this->set_item_generics(item.m_params);
            
            // TODO: Use a different type depding on repr()
            auto enum_type = ::HIR::TypeRef(::HIR::CoreType::Usize);
            
            // TODO: Check types too?
            for(auto& var : item.m_variants)
            {
                TU_IFLET(::HIR::Enum::Variant, var.second, Value, e,
                    TypecheckContext typeck_context { m_crate, m_impl_generics, m_item_generics };
                    typeck_context.m_traits = this->m_traits;
                    DEBUG("Enum value " << p << " - " << var.first);
                    Typecheck_Code( mv$(typeck_context), enum_type, e );
                )
            }
        }
    };
}

void Typecheck_Expressions(::HIR::Crate& crate)
{
    OuterVisitor    visitor { crate };
    visitor.visit_crate( crate );
}