/*
 * MRustC - Mutabah's Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 * 
 * types.cpp
 * - Backing code for the TypeRef class
 *
 * Handles a chunk of type resolution (merging) and matching/comparing types
 */
#include "types.hpp"
#include "ast/ast.hpp"

const char* coretype_name(const eCoreType ct ) {
    switch(ct)
    {
    case CORETYPE_INVAL:return "-";
    case CORETYPE_ANY:  return "_";
    case CORETYPE_CHAR: return "char";
    case CORETYPE_BOOL: return "bool";
    case CORETYPE_UINT: return "usize";
    case CORETYPE_INT:  return "isize";
    case CORETYPE_U8:   return "u8";
    case CORETYPE_I8:   return "i8";
    case CORETYPE_U16:  return "u16";
    case CORETYPE_I16:  return "i16";
    case CORETYPE_U32:  return "u32";
    case CORETYPE_I32:  return "i32";
    case CORETYPE_U64:  return "u64";
    case CORETYPE_I64:  return "i64";
    case CORETYPE_F32:  return "f32";
    case CORETYPE_F64:  return "f64";
    }
    DEBUG("Unknown core type?! " << ct);
    return "NFI";
}

/// Replace this type reference with a dereferenced version
bool TypeRef::deref(bool is_implicit)
{
    switch(m_class)
    {
    case TypeRef::NONE:
        throw ::std::runtime_error("Dereferencing ! - bugcheck");
    case TypeRef::ANY:
        // TODO: Check if the _ is bounded by Deref<Output=?>, if so use that
        throw ::std::runtime_error("Dereferencing _");
        break;
    case TypeRef::UNIT:
        throw ::std::runtime_error("Dereferencing ()");
    case TypeRef::PRIMITIVE:
        throw ::std::runtime_error("Dereferencing a primtive type");
    case TypeRef::GENERIC:
        throw ::std::runtime_error("Dereferencing a generic");
    case TypeRef::FUNCTION:
        throw ::std::runtime_error("Dereferencing a function");
    case TypeRef::REFERENCE:
        *this = m_inner_types[0];
        return true;
    case TypeRef::POINTER:
        // raw pointers can't be implicitly dereferenced
        if( is_implicit )
            return false;
        *this = m_inner_types[0];
        return true;
    case TypeRef::TUPLE:
    case TypeRef::ARRAY:
    case TypeRef::PATH:
        throw ::std::runtime_error("TODO: Search for an impl of Deref");
    case TypeRef::MULTIDST:
        throw ::std::runtime_error("TODO: TypeRef::deref on MULTIDST");
    }
    throw ::std::runtime_error("BUGCHECK: Fell off end of TypeRef::deref");
}

/// Merge the contents of the passed type with this type
/// 
/// \note Both types must be of the same form (i.e. both be tuples)
void TypeRef::merge_with(const TypeRef& other)
{
    // Ignore if other is wildcard
    //if( other.m_class == TypeRef::ANY ) {
    //    assert(other.m_inner_types.size() == 0 && "TODO: merge_with on bounded _");
    //    return;
    //}
   
    // If this is a wildcard, then replace with the other type 
    if( m_class == TypeRef::ANY ) {
        if( m_inner_types.size() && m_inner_types.size() != other.m_inner_types.size() )
            throw ::std::runtime_error("TypeRef::merge_with - Handle bounded wildcards");
        *this = other;
        return;
    }
    
    // If classes don't match, then merge is impossible
    if( m_class != other.m_class )
        throw ::std::runtime_error("TypeRef::merge_with - Types not compatible");
    
    // If both types are concrete, then they must be the same
    if( is_concrete() && other.is_concrete() )
    {
        if( *this != other )
            throw ::std::runtime_error("TypeRef::merge_with - Types not compatible");
        return;
    }
        
    
    switch(m_class)
    {
    case TypeRef::NONE:
    case TypeRef::ANY:
    case TypeRef::UNIT:
    case TypeRef::PRIMITIVE:
        throw ::std::runtime_error("TypeRef::merge_with - Reached concrete/wildcard");
    case TypeRef::FUNCTION:
        if( m_inner_types.size() != other.m_inner_types.size() )
            throw ::std::runtime_error("TypeRef::merge_with - Types not compatible [function sz]");
        // - fall through to tuple code
    case TypeRef::TUPLE:
        // Other is known not to be wildcard, and is also a tuple, so it must be the same size
        if( m_inner_types.size() != other.m_inner_types.size() )
            throw ::std::runtime_error("TypeRef::merge_with - Types not compatible [tuple sz]");
        for(unsigned int i = 0; i < m_inner_types.size(); i ++)
        {
            m_inner_types[i].merge_with( other.m_inner_types[i] );
        }
        break;
    case TypeRef::REFERENCE:
    case TypeRef::POINTER:
        if( m_is_inner_mutable != other.m_is_inner_mutable )
            throw ::std::runtime_error("TypeRef::merge_with - Types not compatible [inner mut]");
        assert( m_inner_types.size() == 1 );
        assert( other.m_inner_types.size() == 1 );
        m_inner_types[0].merge_with( other.m_inner_types[0] );
        break;
    case TypeRef::ARRAY:
        throw ::std::runtime_error("TODO: TypeRef::merge_with on ARRAY");
    case TypeRef::GENERIC:
        throw ::std::runtime_error("TODO: TypeRef::merge_with on GENERIC");
    case TypeRef::PATH:
        throw ::std::runtime_error("TODO: TypeRef::merge_with on PATH");
    case TypeRef::MULTIDST:
        throw ::std::runtime_error("TODO: TypeRef::merge_with on MULTIDST");
    }
}

/// Resolve all Generic/Argument types to the value returned by the passed closure
/// 
/// Replaces every instance of a TypeRef::GENERIC with the value returned from the passed
/// closure.
void TypeRef::resolve_args(::std::function<TypeRef(const char*)> fcn)
{
    DEBUG("" << *this);
    switch(m_class)
    {
    case TypeRef::NONE:
        throw ::std::runtime_error("TypeRef::resolve_args on !");
    case TypeRef::ANY:
        // TODO: Is resolving args on an ANY an erorr?
        break;
    case TypeRef::UNIT:
    case TypeRef::PRIMITIVE:
        break;
    case TypeRef::FUNCTION:
    case TypeRef::TUPLE:
    case TypeRef::REFERENCE:
    case TypeRef::POINTER:
    case TypeRef::ARRAY:
        for( auto& t : m_inner_types )
            t.resolve_args(fcn);
        break;
    case TypeRef::GENERIC:
        *this = fcn(m_path[0].name().c_str());
        break;
    case TypeRef::PATH:
        m_path.resolve_args(fcn);
        break;
    case TypeRef::MULTIDST:
        for(auto& t : m_inner_types )
            t.resolve_args(fcn);
        break;
    }
}

/// Match this type against another type, calling the provided function for all generics found in this
///
/// \param other    Type containing (possibly) concrete types
/// \param fcn  Function to call for all generics (called with matching type from \a other)
/// This is used to handle extracting types passsed to methods/enum variants
void TypeRef::match_args(const TypeRef& other, ::std::function<void(const char*,const TypeRef&)> fcn) const
{
    // If this type is a generic, then call the closure with the other type
    if( m_class == TypeRef::GENERIC ) {
        fcn( m_path[0].name().c_str(), other );
        return ;
    }
    
    // If the other type is a wildcard, early return
    // - TODO - Might want to restrict the other type to be of the same form as this type
    if( other.m_class == TypeRef::ANY )
        return;
    
    DEBUG("this = " << *this << ", other = " << other);
    
    // Any other case, it's a "pattern" match
    if( m_class != other.m_class )
        throw ::std::runtime_error("Type mismatch (class)");
    switch(m_class)
    {
    case TypeRef::NONE:
        throw ::std::runtime_error("TypeRef::match_args on !");
    case TypeRef::ANY:
        // Wait, isn't this an error?
        throw ::std::runtime_error("Encountered '_' in match_args");
    case TypeRef::UNIT:
        break;
    case TypeRef::PRIMITIVE:
        // TODO: Should check if the type matches
        if( m_core_type != other.m_core_type )
            throw ::std::runtime_error("Type mismatch (core)");
        break;
    case TypeRef::FUNCTION:
        if( m_path[0].name() != other.m_path[0].name() )
            throw ::std::runtime_error("Type mismatch (function abo)");
        if( m_inner_types.size() != other.m_inner_types.size() )
            throw ::std::runtime_error("Type mismatch (function size)");
    case TypeRef::TUPLE:
        if( m_inner_types.size() != other.m_inner_types.size() )
            throw ::std::runtime_error("Type mismatch (tuple size)");
        for(unsigned int i = 0; i < m_inner_types.size(); i ++ )
            m_inner_types[i].match_args( other.m_inner_types[i], fcn );
        break;
    case TypeRef::REFERENCE:
    case TypeRef::POINTER:
        if( m_is_inner_mutable != other.m_is_inner_mutable )
            throw ::std::runtime_error("Type mismatch (inner mutable)");
        m_inner_types[0].match_args( other.m_inner_types[0], fcn );
        break;
    case TypeRef::ARRAY:
        m_inner_types[0].match_args( other.m_inner_types[0], fcn );
        if(m_size_expr.get())
        {
            throw ::std::runtime_error("TODO: Sized array match_args");
        }
        break;
    case TypeRef::GENERIC:
        throw ::std::runtime_error("Encountered GENERIC in match_args");
    case TypeRef::PATH:
        m_path.match_args(other.m_path, fcn);
        break;
    case TypeRef::MULTIDST:
        throw ::std::runtime_error("TODO: TypeRef::match_args on MULTIDST");
    }
}

bool TypeRef::impls_wildcard(AST::Crate& crate, const AST::Path& trait) const
{
    switch(m_class)
    {
    case TypeRef::NONE:
        throw CompileError::BugCheck("TypeRef::impls_wildcard on !");
    case TypeRef::ANY:
        throw CompileError::BugCheck("TypeRef::impls_wildcard on _");
    // Generics are an error?
    case GENERIC:
        // TODO: Include an annotation to the TypeParams structure relevant to this type
        // - Allows searching the params for the impl, without having to pass the params down
        throw CompileError::Todo("TypeRef::impls_wildcard - param");
    // Primitives always impl
    case UNIT:
    case PRIMITIVE:
        return true;
    // Functions are pointers (currently), so they implement the trait
    case FUNCTION:
        return true;
    // Pointers/arrays inherit directly
    case REFERENCE:
        return crate.find_impl(trait, sub_types()[0], nullptr);
    case POINTER:
        return crate.find_impl(trait, sub_types()[0], nullptr);
    case ARRAY:
        return crate.find_impl(trait, sub_types()[0], nullptr);
    // Tuples just destructure
    case TUPLE:
        for( const auto& fld : sub_types() )
        {
            if( !crate.find_impl(trait, fld, nullptr) )
                return false;
        }
        return true;
    // MultiDST is special - It only impls if this trait is in the list
    //  (or if a listed trait requires/impls the trait)
    case MULTIDST:
        throw CompileError::Todo("TypeRef::impls_wildcard - MULTIDST");
    // Path types destructure
    case PATH:
        // - structs need all fields to impl this trait (cache result)
        // - same for enums, tuples, arrays, pointers...
        // - traits check the Self bounds
        // CATCH: Need to handle recursion, keep a list of currently processing paths and assume true if found
        switch(m_path.binding().type())
        {
        case AST::PathBinding::STRUCT: {
            const auto &s = m_path.binding().bound_struct();
            GenericResolveClosure   resolve_fn( s.params(), m_path.nodes().back().args() );
            for(const auto& fld : s.fields())
            {
                TypeRef fld_ty = fld.data;
                fld_ty.resolve_args( resolve_fn );
                DEBUG("- Fld '" << fld.name << "' := " << fld.data << " => " << fld_ty);
                // TODO: Defer failure until after all fields are processed
                if( !crate.find_impl(trait, fld_ty, nullptr) )
                    return false;
            }
            return true; }
        case AST::PathBinding::ENUM: {
            const auto& i = m_path.binding().bound_enum();
            GenericResolveClosure   resolve_fn( i.params(), m_path.nodes().back().args() );
            for( const auto& var : i.variants() )
            {
                for( const auto& ty : var.m_sub_types )
                {
                    TypeRef real_ty = ty;
                    real_ty.resolve_args( resolve_fn );
                    DEBUG("- Var '" << var.m_name << "' := " << ty << " => " << real_ty);
                    // TODO: Defer failure until after all fields are processed
                    if( !crate.find_impl(trait, real_ty, nullptr) )
                        return false;
                }
            }
            return true; }
        default:
            throw CompileError::Todo("wildcard impls - auto determine path");
        }
    }
    throw CompileError::BugCheck("TypeRef::impls_wildcard - Fell off end");
}

/// Checks if the type is fully bounded
bool TypeRef::is_concrete() const
{
    switch(m_class)
    {
    case TypeRef::NONE:
        throw ::std::runtime_error("TypeRef::is_concrete on !");
    case TypeRef::ANY:
        return false;
    case TypeRef::UNIT:
    case TypeRef::PRIMITIVE:
        return true;
    case TypeRef::FUNCTION:
    case TypeRef::TUPLE:
    case TypeRef::REFERENCE:
    case TypeRef::POINTER:
    case TypeRef::ARRAY:
        for(const auto& t : m_inner_types )
            if( not t.is_concrete() )
                return false;
        return true;
    case TypeRef::GENERIC:
        // Well, I guess a generic param is "concrete"
        return true;
    case TypeRef::PATH:
        for(const auto& n : m_path.nodes())
        {
            for(const auto& p : n.args())
                if( not p.is_concrete() )
                    return false;
        }
        return true;
    case TypeRef::MULTIDST:
        for(const auto& t : m_inner_types )
            if( not t.is_concrete() )
                return false;
        return true;
    }
    throw ::std::runtime_error( FMT("BUGCHECK - Invalid type class on " << *this) );
}

int TypeRef::equal_no_generic(const TypeRef& x) const
{
    //DEBUG(*this << ", " << x);
    if( m_class == TypeRef::GENERIC ) //|| x.m_class == TypeRef::GENERIC )
        return 1;
    if( m_class != x.m_class )  return -1;
    switch(m_class)
    {
    case TypeRef::NONE:
    case TypeRef::UNIT:
        return 0;
    case TypeRef::ANY:
        return 0;
    case TypeRef::PRIMITIVE:
        if( m_core_type != x.m_core_type )  return -1;
        return 0;
    case TypeRef::FUNCTION:
        if( m_path[0].name() != x.m_path[0].name() )    return -1;
        throw CompileError::Todo("TypeRef::equal_no_generic - FUNCTION");
    case TypeRef::PATH:
        return m_path.equal_no_generic( x.m_path );
    case TypeRef::REFERENCE:
    case TypeRef::POINTER:
        if( m_is_inner_mutable != x.m_is_inner_mutable )
            return -1;
        return m_inner_types[0].equal_no_generic( x.m_inner_types[0] );
    case TypeRef::ARRAY:
        if( m_size_expr.get() || x.m_size_expr.get() )
            throw CompileError::Todo("TypeRef::equal_no_generic - sized array");
        return m_inner_types[0].equal_no_generic( x.m_inner_types[0] );
    case TypeRef::TUPLE: {
        bool fuzzy = false;
        if( m_inner_types.size() != x.m_inner_types.size() )
            return -1;
        for( unsigned int i = 0; i < m_inner_types.size(); i ++ )
        {
            int rv = m_inner_types[i].equal_no_generic( x.m_inner_types[i] );
            if(rv < 0)  return -1;
            if(rv > 0)  fuzzy = true;
        }
        return (fuzzy ? 1 : 0); }
    case TypeRef::MULTIDST:
        throw CompileError::Todo("TypeRef::equal_no_generic - MULTIDST");
    case TypeRef::GENERIC:
        throw CompileError::BugCheck("equal_no_generic - Generic should have been handled above");
    }
    throw CompileError::BugCheck("equal_no_generic - Ran off end");
}
Ordering TypeRef::ord(const TypeRef& x) const
{
    Ordering    rv;
    
    rv = ::ord( (unsigned)m_class, (unsigned)x.m_class );
    if(rv != OrdEqual)  return rv;
    
    switch(m_class)
    {
    case TypeRef::NONE:
        return OrdEqual;
    case TypeRef::ANY:
    case TypeRef::UNIT:
        return OrdEqual;
    case TypeRef::PRIMITIVE:
        rv = ::ord( (unsigned)m_core_type, (unsigned)x.m_core_type );
        if(rv != OrdEqual)  return rv;
        return OrdEqual;
    case TypeRef::FUNCTION:
        rv = ::ord(m_path[0].name(),x.m_path[0].name());
        if(rv != OrdEqual)  return rv;
        return ::ord(m_inner_types, x.m_inner_types);
    case TypeRef::TUPLE:
        return ::ord(m_inner_types, x.m_inner_types);
        //return m_inner_types == x.m_inner_types;
    case TypeRef::REFERENCE:
    case TypeRef::POINTER:
        rv = ::ord(m_is_inner_mutable, x.m_is_inner_mutable);
        if(rv != OrdEqual)  return rv;
        return ::ord(m_inner_types, x.m_inner_types);
    case TypeRef::ARRAY:
        rv = m_inner_types[0].ord( x.m_inner_types[0] );
        if(rv != OrdEqual)  return rv;
        if(m_size_expr.get())
        {
            throw ::std::runtime_error("TODO: Sized array comparisons");
        }
        return OrdEqual;
    case TypeRef::GENERIC:
        if( m_tagged_ptr != x.m_tagged_ptr )
        {
            DEBUG(*this << " == " << x);
            if( m_tagged_ptr )   DEBUG("- (L) " << *type_params_ptr());
            if( x.m_tagged_ptr ) DEBUG("- (R) " << *x.type_params_ptr());
            throw ::std::runtime_error("BUGCHECK - Can't compare mismatched generic types");
        }
        else {
        }
        return m_path.ord( x.m_path );
    case TypeRef::PATH:
        return m_path.ord( x.m_path );
    case TypeRef::MULTIDST:
        return ::ord(m_inner_types, x.m_inner_types);
    }
    throw ::std::runtime_error(FMT("BUGCHECK - Unhandled TypeRef class '" << m_class << "'"));
}

::std::ostream& operator<<(::std::ostream& os, const eCoreType ct) {
    return os << coretype_name(ct);
}

::std::ostream& operator<<(::std::ostream& os, const TypeRef& tr) {
    //os << "TypeRef(";
    switch(tr.m_class)
    {
    case TypeRef::NONE:
        os << "!";
        break;
    case TypeRef::ANY:
        //os << "TagAny";
        os << "_";
        if( tr.m_inner_types.size() ) {
            os << ": " << tr.m_inner_types << "";
        }
        break;
    case TypeRef::UNIT:
        //os << "TagUnit";
        os << "()";
        break;
    case TypeRef::PRIMITIVE:
        //os << "TagPrimitive, " << tr.m_core_type;
        os << tr.m_core_type;
        break;
    case TypeRef::FUNCTION:
        if( tr.m_path[0].name() != "" )
            os << "extern \"" << tr.m_path[0].name() << "\" ";
        os << "fn (";
        for( unsigned int i = 0; i < tr.m_inner_types.size()-1; i ++ )
            os << tr.m_inner_types[i] << ", ";
        os << ") -> " << tr.m_inner_types.back();
        break;
    case TypeRef::TUPLE:
        //os << "TagTuple, {" << tr.m_inner_types << "}";
        os << "( ";
        for( const auto& it : tr.m_inner_types )
            os << it << ", ";
        os << ")";
        break;
    case TypeRef::REFERENCE:
        //os << "TagReference, " << (tr.m_is_inner_mutable ? "mut" : "const") << ", " << tr.m_inner_types[0];
        os << "&" << (tr.m_is_inner_mutable ? "mut " : "") << tr.m_inner_types[0];
        break;
    case TypeRef::POINTER:
        //os << "TagPointer, " << (tr.m_is_inner_mutable ? "mut" : "const") << ", " << tr.m_inner_types[0];
        os << "*" << (tr.m_is_inner_mutable ? "mut" : "const") << " " << tr.m_inner_types[0];
        break;
    case TypeRef::ARRAY:
        os << "[" << tr.m_inner_types[0];
        if( tr.m_size_expr.get() )
            os << "; " << *tr.m_size_expr;
        os << "]";
        break;
    case TypeRef::GENERIC:
        os << "/* arg */ " << tr.m_path[0].name();
        break;
    case TypeRef::PATH:
        os << tr.m_path;
        break;
    case TypeRef::MULTIDST:
        os << "(";
        for( const auto& it : tr.m_inner_types ) {
            if( &it != &tr.m_inner_types.front() )
                os << "+";
            os << it;
        }
        os << ")";
        break;
    }
    //os << ")";
    return os;
}

void operator% (::Serialiser& s, eCoreType ct) {
    s << coretype_name(ct);
}
void operator% (::Deserialiser& d, eCoreType& ct) {
    ::std::string n;
    d.item(n);
    /* */if(n == "-")   ct = CORETYPE_INVAL;
    else if(n == "_")   ct = CORETYPE_ANY;
    else if(n == "bool")  ct = CORETYPE_BOOL;
    else if(n == "char")  ct = CORETYPE_CHAR;
    else if(n == "usize") ct = CORETYPE_UINT;
    else if(n == "isize") ct = CORETYPE_INT;
    else if(n == "u8")    ct = CORETYPE_U8;
    else if(n == "i8")    ct = CORETYPE_I8;
    else if(n == "u16")   ct = CORETYPE_U16;
    else if(n == "i16")   ct = CORETYPE_I16;
    else if(n == "u32")   ct = CORETYPE_U32;
    else if(n == "i32")   ct = CORETYPE_I32;
    else if(n == "u64")   ct = CORETYPE_U64;
    else if(n == "i64")   ct = CORETYPE_I64;
    else if(n == "f32")   ct = CORETYPE_F32;
    else if(n == "f64")   ct = CORETYPE_F64;
    else
        throw ::std::runtime_error("Deserialise failure - coretype " + n);
}
const char* TypeRef::class_name(TypeRef::Class c) {
    switch(c)
    {
    #define _(x)    case TypeRef::x: return #x;
    _(NONE)
    _(ANY)
    _(UNIT)
    _(PRIMITIVE)
    _(FUNCTION)
    _(TUPLE)
    _(REFERENCE)
    _(POINTER)
    _(ARRAY)
    _(GENERIC)
    _(PATH)
    _(MULTIDST)
    #undef _
    }
    return "NFI";
}
void operator>>(::Deserialiser& d, TypeRef::Class& c) {
    ::std::string n;
    d.item(n);
    #define _(x) if(n == #x) c = TypeRef::x;
    /**/ _(ANY)
    else _(NONE)
    else _(UNIT)
    else _(PRIMITIVE)
    else _(FUNCTION)
    else _(TUPLE)
    else _(REFERENCE)
    else _(POINTER)
    else _(ARRAY)
    else _(GENERIC)
    else _(PATH)
    else _(MULTIDST)
    else
        throw ::std::runtime_error("Deserialise failure - " + n);
    #undef _
}
SERIALISE_TYPE(TypeRef::, "TypeRef", {
    s << class_name(m_class);
    if(m_class == PRIMITIVE)
        s << coretype_name(m_core_type);
    s << m_inner_types;
    if(m_class == REFERENCE || m_class == POINTER)
        s << m_is_inner_mutable;
    s << m_size_expr;
    s << m_path;
},{
    s >> m_class;
    if(m_class == PRIMITIVE)
        s % m_core_type;
    s.item( m_inner_types );
    if(m_class == REFERENCE || m_class == POINTER)
        s.item( m_is_inner_mutable );
    bool size_expr_present;
    s.item(size_expr_present);
    if( size_expr_present )
        m_size_expr = AST::ExprNode::from_deserialiser(s);
    else
        m_size_expr.reset();
    s.item( m_path );
})


void PrettyPrintType::print(::std::ostream& os) const
{
    #if 1
    os << m_type;
    #else
    switch(m_type.m_class)
    {
    case TypeRef::ANY:
        os << "_";
        if( m_type.m_inner_types.size() ) {
            os << "/* : " << m_type.m_inner_types << "*/";
        }
        break;
    case TypeRef::UNIT:
        os << "()";
        break;
    case TypeRef::PRIMITIVE:
        os << m_type.m_core_type;
        break;
    case TypeRef::FUNCTION:
        if( m_type.m_path[0].name() != "" )
            os << "extern \"" << m_type.m_path[0].name() << "\" ";
        os << "fn (";
        for( unsigned int i = 0; i < m_type.m_inner_types.size()-1; i ++ )
            os << m_type.m_inner_types[i].print_pretty() << ", ";
        os << ") -> " << m_type.m_inner_types.back().print_pretty();
        break;
    case TypeRef::TUPLE:
        os << "(";
        for(const auto& t : m_type.m_inner_types)
            os << t.print_pretty() << ",";
        os << ")";
        break;
    case TypeRef::REFERENCE:
        os << "&" << (m_type.m_is_inner_mutable ? "mut " : "") << m_type.m_inner_types[0].print_pretty();
        break;
    case TypeRef::POINTER:
        os << "*" << (m_type.m_is_inner_mutable ? "mut" : "const") << " " << m_type.m_inner_types[0].print_pretty();
        break;
    case TypeRef::ARRAY:
        os << "[" << m_type.m_inner_types[0].print_pretty() << ", " << m_type.m_size_expr << "]";
        break;
    case TypeRef::GENERIC:
        os << m_type.m_path[0].name();
        break;
    case TypeRef::PATH:
        os << m_type.m_path;
        break;
    }
    #endif
}

::std::ostream& operator<<(::std::ostream& os, const PrettyPrintType& v)
{
    v.print(os);
    return os;
}
