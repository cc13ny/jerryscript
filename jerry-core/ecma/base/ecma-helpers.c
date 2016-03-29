/* Copyright 2014-2016 Samsung Electronics Co., Ltd.
 * Copyright 2016 University of Szeged.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** \addtogroup ecma ECMA
 * @{
 *
 * \addtogroup ecmahelpers Helpers for operations with ECMA data types
 * @{
 */

#include "ecma-alloc.h"
#include "ecma-gc.h"
#include "ecma-globals.h"
#include "ecma-helpers.h"
#include "ecma-lcache.h"
#include "jrt-bit-fields.h"
#include "byte-code.h"
#include "re-compiler.h"

/**
 * The ecma object types must be lower than the container mask.
 */
JERRY_STATIC_ASSERT (ECMA_OBJECT_TYPE_MASK >= ECMA_OBJECT_TYPE__MAX,
                     ecma_object_types_must_be_lower_than_the_container_mask);

/**
 * The ecma lexical environment types must be lower than the container mask.
 */
JERRY_STATIC_ASSERT (ECMA_OBJECT_TYPE_MASK >= ECMA_LEXICAL_ENVIRONMENT_TYPE__MAX,
                     ecma_lexical_environment_types_must_be_lower_than_the_container_mask);

/**
 * The ecma built in flag must follow the object type.
 */
JERRY_STATIC_ASSERT (ECMA_OBJECT_TYPE_MASK + 1 == ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV,
                     ecma_built_in_flag_must_follow_the_object_type);

/**
 * The ecma gc visited flag must follow the built in flag.
 */
JERRY_STATIC_ASSERT (ECMA_OBJECT_FLAG_GC_VISITED == (ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV << 1),
                     ecma_gc_visited_flag_must_follow_the_built_in_flag);

/**
 * The ecma extensible flag must follow the gc visited flag.
 */
JERRY_STATIC_ASSERT (ECMA_OBJECT_FLAG_EXTENSIBLE == (ECMA_OBJECT_FLAG_GC_VISITED << 1),
                     ecma_extensible_flag_must_follow_the_gc_visited_flag);

/**
 * The ecma object ref one must follow the extensible flag.
 */
JERRY_STATIC_ASSERT (ECMA_OBJECT_REF_ONE == (ECMA_OBJECT_FLAG_EXTENSIBLE << 1),
                     ecma_object_ref_one_must_follow_the_extensible_flag);

/**
 * The ecma object max ref does not fill the remaining bits.
 */
JERRY_STATIC_ASSERT ((ECMA_OBJECT_MAX_REF | (ECMA_OBJECT_REF_ONE - 1)) == UINT16_MAX,
                     ecma_object_max_ref_does_not_fill_the_remaining_bits);

/**
 * Create an object with specified prototype object
 * (or NULL prototype if there is not prototype for the object)
 * and value of 'Extensible' attribute.
 *
 * Reference counter's value will be set to one.
 *
 * @return pointer to the object's descriptor
 */
ecma_object_t *
ecma_create_object (ecma_object_t *prototype_object_p, /**< pointer to prototybe of the object (or NULL) */
                    bool is_extensible, /**< value of extensible attribute */
                    ecma_object_type_t type) /**< object type */
{
  ecma_object_t *new_object_p = ecma_alloc_object ();

  uint16_t type_flags = (uint16_t) type;

  if (is_extensible)
  {
    type_flags = (uint16_t) (type_flags | ECMA_OBJECT_FLAG_EXTENSIBLE);
  }

  new_object_p->type_flags_refs = type_flags;

  ecma_init_gc_info (new_object_p);

  new_object_p->property_list_or_bound_object_cp = MEM_CP_NULL;

  ECMA_SET_POINTER (new_object_p->prototype_or_outer_reference_cp,
                    prototype_object_p);

  return new_object_p;
} /* ecma_create_object */

/**
 * Create a declarative lexical environment with specified outer lexical environment
 * (or NULL if the environment is not nested).
 *
 * See also: ECMA-262 v5, 10.2.1.1
 *
 * Reference counter's value will be set to one.
 *
 * @return pointer to the descriptor of lexical environment
 */
ecma_object_t *
ecma_create_decl_lex_env (ecma_object_t *outer_lexical_environment_p) /**< outer lexical environment */
{
  ecma_object_t *new_lexical_environment_p = ecma_alloc_object ();

  uint16_t type = ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_LEXICAL_ENVIRONMENT_DECLARATIVE;
  new_lexical_environment_p->type_flags_refs = type;

  ecma_init_gc_info (new_lexical_environment_p);

  new_lexical_environment_p->property_list_or_bound_object_cp = MEM_CP_NULL;

  ECMA_SET_POINTER (new_lexical_environment_p->prototype_or_outer_reference_cp,
                    outer_lexical_environment_p);

  return new_lexical_environment_p;
} /* ecma_create_decl_lex_env */

/**
 * Create a object lexical environment with specified outer lexical environment
 * (or NULL if the environment is not nested), binding object and provideThis flag.
 *
 * See also: ECMA-262 v5, 10.2.1.2
 *
 * Reference counter's value will be set to one.
 *
 * @return pointer to the descriptor of lexical environment
 */
ecma_object_t *
ecma_create_object_lex_env (ecma_object_t *outer_lexical_environment_p, /**< outer lexical environment */
                            ecma_object_t *binding_obj_p, /**< binding object */
                            bool provide_this) /**< provideThis flag */
{
  JERRY_ASSERT (binding_obj_p != NULL
                && !ecma_is_lexical_environment (binding_obj_p));

  ecma_object_t *new_lexical_environment_p = ecma_alloc_object ();

  uint16_t type;

  if (provide_this)
  {
    type = ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND;
  }
  else
  {
    type = ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_LEXICAL_ENVIRONMENT_OBJECT_BOUND;
  }

  new_lexical_environment_p->type_flags_refs = type;

  ecma_init_gc_info (new_lexical_environment_p);

  ECMA_SET_NON_NULL_POINTER (new_lexical_environment_p->property_list_or_bound_object_cp,
                             binding_obj_p);

  ECMA_SET_POINTER (new_lexical_environment_p->prototype_or_outer_reference_cp,
                    outer_lexical_environment_p);

  return new_lexical_environment_p;
} /* ecma_create_object_lex_env */

/**
 * Check if the object is lexical environment.
 */
inline bool __attr_pure___
ecma_is_lexical_environment (const ecma_object_t *object_p) /**< object or lexical environment */
{
  JERRY_ASSERT (object_p != NULL);

  uint32_t full_type = object_p->type_flags_refs & (ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_OBJECT_TYPE_MASK);

  return full_type >= (ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV | ECMA_LEXICAL_ENVIRONMENT_TYPE_START);
} /* ecma_is_lexical_environment */

/**
 * Get value of [[Extensible]] object's internal property.
 */
inline bool __attr_pure___
ecma_get_object_extensible (const ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  return (object_p->type_flags_refs & ECMA_OBJECT_FLAG_EXTENSIBLE) != 0;
} /* ecma_get_object_extensible */

/**
 * Set value of [[Extensible]] object's internal property.
 */
inline void
ecma_set_object_extensible (ecma_object_t *object_p, /**< object */
                            bool is_extensible) /**< value of [[Extensible]] */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  if (is_extensible)
  {
    object_p->type_flags_refs = (uint16_t) (object_p->type_flags_refs | ECMA_OBJECT_FLAG_EXTENSIBLE);
  }
  else
  {
    object_p->type_flags_refs = (uint16_t) (object_p->type_flags_refs & ~ECMA_OBJECT_FLAG_EXTENSIBLE);
  }
} /* ecma_set_object_extensible */

/**
 * Get object's internal implementation-defined type.
 */
inline ecma_object_type_t __attr_pure___
ecma_get_object_type (const ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  return (ecma_object_type_t) (object_p->type_flags_refs & ECMA_OBJECT_TYPE_MASK);
} /* ecma_get_object_type */

/**
 * Set object's internal implementation-defined type.
 */
inline void
ecma_set_object_type (ecma_object_t *object_p, /**< object */
                      ecma_object_type_t type) /**< type */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!(object_p->type_flags_refs & ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV));

  object_p->type_flags_refs = (uint16_t) ((object_p->type_flags_refs & ~ECMA_OBJECT_TYPE_MASK) | type);
} /* ecma_set_object_type */

/**
 * Get object's prototype.
 */
inline ecma_object_t *__attr_pure___
ecma_get_object_prototype (const ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  return ECMA_GET_POINTER (ecma_object_t,
                           object_p->prototype_or_outer_reference_cp);
} /* ecma_get_object_prototype */

/**
 * Check if the object is a built-in object
 *
 * @return true / false
 */
inline bool __attr_pure___
ecma_get_object_is_builtin (const ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p));

  return (object_p->type_flags_refs & ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV) != 0;
} /* ecma_get_object_is_builtin */

/**
 * Set flag indicating whether the object is a built-in object
 */
inline void
ecma_set_object_is_builtin (ecma_object_t *object_p) /**< object */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!(object_p->type_flags_refs & ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV));
  JERRY_ASSERT ((object_p->type_flags_refs & ECMA_OBJECT_TYPE_MASK) < ECMA_LEXICAL_ENVIRONMENT_TYPE_START);

  object_p->type_flags_refs = (uint16_t) (object_p->type_flags_refs | ECMA_OBJECT_FLAG_BUILT_IN_OR_LEXICAL_ENV);
} /* ecma_set_object_is_builtin */

/**
 * Get type of lexical environment.
 */
inline ecma_lexical_environment_type_t __attr_pure___
ecma_get_lex_env_type (const ecma_object_t *object_p) /**< lexical environment */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p));

  return (ecma_lexical_environment_type_t) (object_p->type_flags_refs & ECMA_OBJECT_TYPE_MASK);
} /* ecma_get_lex_env_type */

/**
 * Get outer reference of lexical environment.
 */
inline ecma_object_t *__attr_pure___
ecma_get_lex_env_outer_reference (const ecma_object_t *object_p) /**< lexical environment */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p));

  return ECMA_GET_POINTER (ecma_object_t,
                           object_p->prototype_or_outer_reference_cp);
} /* ecma_get_lex_env_outer_reference */

/**
 * Get object's/lexical environment's property list.
 *
 * See also:
 *          ecma_op_object_get_property_names
 */
inline ecma_property_t *__attr_pure___
ecma_get_property_list (const ecma_object_t *object_p) /**< object or lexical environment */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p)
                || ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_DECLARATIVE);

  return ECMA_GET_POINTER (ecma_property_t,
                           object_p->property_list_or_bound_object_cp);
} /* ecma_get_property_list */

/**
 * Set object's/lexical environment's property list.
 *
 * See also:
 *          ecma_op_object_get_property_names
 */
static inline void
ecma_set_property_list (ecma_object_t *object_p, /**< object or lexical environment */
                        ecma_property_t *property_list_p) /**< properties' list */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (!ecma_is_lexical_environment (object_p)
                || ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_DECLARATIVE);

  ECMA_SET_POINTER (object_p->property_list_or_bound_object_cp,
                    property_list_p);
} /* ecma_set_property_list */

/**
 * Get lexical environment's 'provideThis' property
 */
inline bool __attr_pure___
ecma_get_lex_env_provide_this (const ecma_object_t *object_p) /**< object-bound lexical environment */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p));
  JERRY_ASSERT (ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_OBJECT_BOUND
                || ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND);

  return ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND;
} /* ecma_get_lex_env_provide_this */

/**
 * Get lexical environment's bound object.
 */
inline ecma_object_t *__attr_pure___
ecma_get_lex_env_binding_object (const ecma_object_t *object_p) /**< object-bound lexical environment */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (ecma_is_lexical_environment (object_p));
  JERRY_ASSERT (ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_OBJECT_BOUND
                || ecma_get_lex_env_type (object_p) == ECMA_LEXICAL_ENVIRONMENT_THIS_OBJECT_BOUND);

  return ECMA_GET_NON_NULL_POINTER (ecma_object_t,
                                    object_p->property_list_or_bound_object_cp);
} /* ecma_get_lex_env_binding_object */

/**
 * Create internal property in an object and link it into
 * the object's properties' linked-list (at start of the list).
 *
 * @return pointer to newly created property
 */
ecma_property_t *
ecma_create_internal_property (ecma_object_t *object_p, /**< the object */
                               ecma_internal_property_id_t property_id) /**< internal property identifier */
{
  JERRY_ASSERT (ecma_find_internal_property (object_p, property_id) == NULL);

  ecma_property_t *new_property_p = ecma_alloc_property ();

  new_property_p->flags = ECMA_PROPERTY_FLAG_INTERNAL;

  ecma_property_t *list_head_p = ecma_get_property_list (object_p);
  ECMA_SET_POINTER (new_property_p->next_property_p, list_head_p);
  ecma_set_property_list (object_p, new_property_p);

  JERRY_ASSERT (property_id < ECMA_INTERNAL_PROPERTY__COUNT);

  new_property_p->h.internal_property_type = (uint8_t) property_id;
  new_property_p->v.internal_property.value = ECMA_NULL_POINTER;

  return new_property_p;
} /* ecma_create_internal_property */

/**
 * Find internal property in the object's property set.
 *
 * @return pointer to the property, if it is found,
 *         NULL - otherwise.
 */
ecma_property_t *
ecma_find_internal_property (ecma_object_t *object_p, /**< object descriptor */
                             ecma_internal_property_id_t property_id) /**< internal property identifier */
{
  JERRY_ASSERT (object_p != NULL);

  JERRY_ASSERT (property_id != ECMA_INTERNAL_PROPERTY_PROTOTYPE
                && property_id != ECMA_INTERNAL_PROPERTY_EXTENSIBLE);

  for (ecma_property_t *property_p = ecma_get_property_list (object_p);
       property_p != NULL;
       property_p = ECMA_GET_POINTER (ecma_property_t, property_p->next_property_p))
  {
    if ((property_p->flags & ECMA_PROPERTY_FLAG_INTERNAL)
        && property_p->h.internal_property_type == property_id)
    {
      return property_p;
    }
  }

  return NULL;
} /* ecma_find_internal_property */

/**
 * Get an internal property.
 *
 * Warning:
 *         the property must exist
 *
 * @return pointer to the property
 */
ecma_property_t *
ecma_get_internal_property (ecma_object_t *object_p, /**< object descriptor */
                            ecma_internal_property_id_t property_id) /**< internal property identifier */
{
  ecma_property_t *property_p = ecma_find_internal_property (object_p, property_id);

  JERRY_ASSERT (property_p != NULL);

  return property_p;
} /* ecma_get_internal_property */

/**
 * Create named data property with given name, attributes and undefined value
 * in the specified object.
 *
 * @return pointer to newly created property
 */
ecma_property_t *
ecma_create_named_data_property (ecma_object_t *obj_p, /**< object */
                                 ecma_string_t *name_p, /**< property name */
                                 bool is_writable, /**< 'Writable' attribute */
                                 bool is_enumerable, /**< 'Enumerable' attribute */
                                 bool is_configurable) /**< 'Configurable' attribute */
{
  JERRY_ASSERT (obj_p != NULL && name_p != NULL);
  JERRY_ASSERT (ecma_find_named_property (obj_p, name_p) == NULL);

  ecma_property_t *prop_p = ecma_alloc_property ();
  name_p = ecma_copy_or_ref_ecma_string (name_p);

  prop_p->flags = ECMA_PROPERTY_FLAG_NAMEDDATA;

  ECMA_SET_NON_NULL_POINTER (prop_p->v.named_data_property.name_p, name_p);

  if (is_configurable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_CONFIGURABLE);
  }
  if (is_enumerable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_ENUMERABLE);
  }
  if (is_writable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_WRITABLE);
  }

  ecma_set_named_data_property_value (prop_p, ecma_make_simple_value (ECMA_SIMPLE_VALUE_UNDEFINED));

  /*
   * See also:
   *          ecma_op_object_get_property_names
   */
  ecma_property_t *list_head_p = ecma_get_property_list (obj_p);
  ECMA_SET_POINTER (prop_p->next_property_p, list_head_p);
  ecma_set_property_list (obj_p, prop_p);

  ecma_lcache_invalidate (obj_p, name_p, NULL);

  return prop_p;
} /* ecma_create_named_data_property */

/**
 * Create named accessor property with given name, attributes, getter and setter.
 *
 * @return pointer to newly created property
 */
ecma_property_t *
ecma_create_named_accessor_property (ecma_object_t *obj_p, /**< object */
                                     ecma_string_t *name_p, /**< property name */
                                     ecma_object_t *get_p, /**< getter */
                                     ecma_object_t *set_p, /**< setter */
                                     bool is_enumerable, /**< 'enumerable' attribute */
                                     bool is_configurable) /**< 'configurable' attribute */
{
  JERRY_ASSERT (obj_p != NULL && name_p != NULL);
  JERRY_ASSERT (ecma_find_named_property (obj_p, name_p) == NULL);

  ecma_property_t *prop_p = ecma_alloc_property ();
  ecma_getter_setter_pointers_t *getter_setter_pointers_p = ecma_alloc_getter_setter_pointers ();
  name_p = ecma_copy_or_ref_ecma_string (name_p);

  prop_p->flags = ECMA_PROPERTY_FLAG_NAMEDACCESSOR;

  ECMA_SET_NON_NULL_POINTER (prop_p->v.named_accessor_property.name_p, name_p);

  if (is_configurable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_CONFIGURABLE);
  }
  if (is_enumerable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_ENUMERABLE);
  }

  ECMA_SET_NON_NULL_POINTER (prop_p->v.named_accessor_property.getter_setter_pair_cp, getter_setter_pointers_p);

  /*
   * See also:
   *          ecma_op_object_get_property_names
   */
  ecma_property_t *list_head_p = ecma_get_property_list (obj_p);
  ECMA_SET_POINTER (prop_p->next_property_p, list_head_p);
  ecma_set_property_list (obj_p, prop_p);

  /*
   * Should be performed after linking the property into object's property list, because the setters assert that.
   */
  ecma_set_named_accessor_property_getter (obj_p, prop_p, get_p);
  ecma_set_named_accessor_property_setter (obj_p, prop_p, set_p);

  ecma_lcache_invalidate (obj_p, name_p, NULL);

  return prop_p;
} /* ecma_create_named_accessor_property */

/**
 * Find named data property or named access property in specified object.
 *
 * @return pointer to the property, if it is found,
 *         NULL - otherwise.
 */
ecma_property_t *
ecma_find_named_property (ecma_object_t *obj_p, /**< object to find property in */
                          ecma_string_t *name_p) /**< property's name */
{
  JERRY_ASSERT (obj_p != NULL);
  JERRY_ASSERT (name_p != NULL);

  ecma_property_t *property_p;

  if (ecma_lcache_lookup (obj_p, name_p, &property_p))
  {
    return property_p;
  }

  for (property_p = ecma_get_property_list (obj_p);
       property_p != NULL;
       property_p = ECMA_GET_POINTER (ecma_property_t, property_p->next_property_p))
  {
    ecma_string_t *property_name_p;

    if (property_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA)
    {
      property_name_p = ECMA_GET_NON_NULL_POINTER (ecma_string_t,
                                                   property_p->v.named_data_property.name_p);
    }
    else if (property_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR)
    {
      property_name_p = ECMA_GET_NON_NULL_POINTER (ecma_string_t,
                                                   property_p->v.named_accessor_property.name_p);
    }
    else
    {
      continue;
    }

    JERRY_ASSERT (property_name_p != NULL);

    if (ecma_compare_ecma_strings (name_p, property_name_p))
    {
      break;
    }
  }

  ecma_lcache_insert (obj_p, name_p, property_p);

  return property_p;
} /* ecma_find_named_property */

/**
 * Get named data property or named access property in specified object.
 *
 * Warning:
 *         the property must exist
 *
 * @return pointer to the property, if it is found,
 *         NULL - otherwise.
 */
ecma_property_t *
ecma_get_named_property (ecma_object_t *obj_p, /**< object to find property in */
                         ecma_string_t *name_p) /**< property's name */
{
  JERRY_ASSERT (obj_p != NULL);
  JERRY_ASSERT (name_p != NULL);

  ecma_property_t *property_p = ecma_find_named_property (obj_p, name_p);

  JERRY_ASSERT (property_p != NULL);

  return property_p;
} /* ecma_get_named_property */

/**
 * Get named data property in specified object.
 *
 * Warning:
 *         the property must exist and be named data property
 *
 * @return pointer to the property, if it is found,
 *         NULL - otherwise.
 */
ecma_property_t *
ecma_get_named_data_property (ecma_object_t *obj_p, /**< object to find property in */
                              ecma_string_t *name_p) /**< property's name */
{
  JERRY_ASSERT (obj_p != NULL);
  JERRY_ASSERT (name_p != NULL);

  ecma_property_t *property_p = ecma_find_named_property (obj_p, name_p);

  JERRY_ASSERT (property_p != NULL && (property_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA));

  return property_p;
} /* ecma_get_named_data_property */

/**
 * Free the named data property and values it references.
 */
static void
ecma_free_named_data_property (ecma_object_t *object_p, /**< object the property belongs to */
                               ecma_property_t *property_p) /**< the property */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (property_p != NULL && (property_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA));

  ecma_lcache_invalidate (object_p, NULL, property_p);

  ecma_deref_ecma_string (ECMA_GET_NON_NULL_POINTER (ecma_string_t,
                                                     property_p->v.named_data_property.name_p));

  ecma_value_t v = ecma_get_named_data_property_value (property_p);
  ecma_free_value_if_not_object (v);

  ecma_dealloc_property (property_p);
} /* ecma_free_named_data_property */

/**
 * Free the named accessor property and values it references.
 */
static void
ecma_free_named_accessor_property (ecma_object_t *object_p, /**< object the property belongs to */
                                   ecma_property_t *property_p) /**< the property */
{
  JERRY_ASSERT (object_p != NULL);
  JERRY_ASSERT (property_p != NULL && (property_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR));

  ecma_lcache_invalidate (object_p, NULL, property_p);

  ecma_deref_ecma_string (ECMA_GET_NON_NULL_POINTER (ecma_string_t,
                                                     property_p->v.named_accessor_property.name_p));

  ecma_getter_setter_pointers_t *getter_setter_pointers_p;
  getter_setter_pointers_p = ECMA_GET_NON_NULL_POINTER (ecma_getter_setter_pointers_t,
                                                        property_p->v.named_accessor_property.getter_setter_pair_cp);
  ecma_dealloc_getter_setter_pointers (getter_setter_pointers_p);
  ecma_dealloc_property (property_p);
} /* ecma_free_named_accessor_property */

/**
 * Free the internal property and values it references.
 */
static void
ecma_free_internal_property (ecma_property_t *property_p) /**< the property */
{
  JERRY_ASSERT (property_p != NULL && (property_p->flags & ECMA_PROPERTY_FLAG_INTERNAL));

  ecma_internal_property_id_t property_id = (ecma_internal_property_id_t) property_p->h.internal_property_type;
  uint32_t property_value = property_p->v.internal_property.value;

  switch (property_id)
  {
    case ECMA_INTERNAL_PROPERTY_NUMBER_INDEXED_ARRAY_VALUES: /* a collection */
    case ECMA_INTERNAL_PROPERTY_STRING_INDEXED_ARRAY_VALUES: /* a collection */
    {
      ecma_free_values_collection (ECMA_GET_NON_NULL_POINTER (ecma_collection_header_t,
                                                              property_value),
                                   true);

      break;
    }

    case ECMA_INTERNAL_PROPERTY_PRIMITIVE_STRING_VALUE: /* compressed pointer to a ecma_string_t */
    {
      ecma_string_t *str_p = ECMA_GET_NON_NULL_POINTER (ecma_string_t,
                                                        property_value);
      ecma_deref_ecma_string (str_p);

      break;
    }

    case ECMA_INTERNAL_PROPERTY_PRIMITIVE_NUMBER_VALUE: /* pointer to a ecma_number_t */
    {
      ecma_number_t *num_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t,
                                                        property_value);
      ecma_dealloc_number (num_p);

      break;
    }

    case ECMA_INTERNAL_PROPERTY_NATIVE_CODE: /* an external pointer */
    case ECMA_INTERNAL_PROPERTY_NATIVE_HANDLE: /* an external pointer */
    case ECMA_INTERNAL_PROPERTY_FREE_CALLBACK: /* an external pointer */
    {
      ecma_free_external_pointer_in_property (property_p);

      break;
    }

    case ECMA_INTERNAL_PROPERTY_PRIMITIVE_BOOLEAN_VALUE: /* a simple boolean value */
    case ECMA_INTERNAL_PROPERTY_SCOPE: /* a lexical environment */
    case ECMA_INTERNAL_PROPERTY_PARAMETERS_MAP: /* an object */
    case ECMA_INTERNAL_PROPERTY_PROTOTYPE: /* the property's value is located in ecma_object_t */
    case ECMA_INTERNAL_PROPERTY_EXTENSIBLE: /* the property's value is located in ecma_object_t */
    case ECMA_INTERNAL_PROPERTY_CLASS: /* an enum */
    case ECMA_INTERNAL_PROPERTY_BUILT_IN_ID: /* an integer */
    case ECMA_INTERNAL_PROPERTY_BUILT_IN_ROUTINE_DESC: /* an integer */
    case ECMA_INTERNAL_PROPERTY_EXTENSION_ID: /* an integer */
    case ECMA_INTERNAL_PROPERTY_NON_INSTANTIATED_BUILT_IN_MASK_0_31: /* an integer (bit-mask) */
    case ECMA_INTERNAL_PROPERTY_NON_INSTANTIATED_BUILT_IN_MASK_32_63: /* an integer (bit-mask) */
    case ECMA_INTERNAL_PROPERTY_BOUND_FUNCTION_TARGET_FUNCTION:
    {
      break;
    }

    case ECMA_INTERNAL_PROPERTY_BOUND_FUNCTION_BOUND_THIS:
    {
      ecma_free_value_if_not_object (property_value);
      break;
    }

    case ECMA_INTERNAL_PROPERTY_BOUND_FUNCTION_BOUND_ARGS:
    {
      if (property_value != ECMA_NULL_POINTER)
      {
        ecma_free_values_collection (ECMA_GET_NON_NULL_POINTER (ecma_collection_header_t, property_value),
                                     false);
      }

      break;
    }

    case ECMA_INTERNAL_PROPERTY__COUNT: /* not a real internal property type,
                                         * but number of the real internal property types */
    {
      JERRY_UNREACHABLE ();
      break;
    }

    case ECMA_INTERNAL_PROPERTY_CODE_BYTECODE: /* compressed pointer to a bytecode array */
    {
      ecma_bytecode_deref (ECMA_GET_NON_NULL_POINTER (ecma_compiled_code_t, property_value));
      break;
    }

    case ECMA_INTERNAL_PROPERTY_REGEXP_BYTECODE: /* compressed pointer to a regexp bytecode array */
    {
      ecma_compiled_code_t *bytecode_p = ECMA_GET_POINTER (ecma_compiled_code_t, property_value);

      if (bytecode_p != NULL)
      {
        ecma_bytecode_deref (bytecode_p);
      }
      break;
    }
  }

  ecma_dealloc_property (property_p);
} /* ecma_free_internal_property */

/**
 * Free the property and values it references.
 */
void
ecma_free_property (ecma_object_t *object_p, /**< object the property belongs to */
                    ecma_property_t *prop_p) /**< property */
{
  JERRY_ASSERT (object_p != NULL && prop_p != NULL);

  if (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA)
  {
    ecma_free_named_data_property (object_p, prop_p);
  }
  else if (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR)
  {
    ecma_free_named_accessor_property (object_p, prop_p);
  }
  else
  {
    ecma_free_internal_property (prop_p);
  }
} /* ecma_free_property */

/**
 * Delete the object's property.
 *
 * Warning: specified property must be owned by specified object.
 */
void
ecma_delete_property (ecma_object_t *obj_p, /**< object */
                      ecma_property_t *prop_p) /**< property */
{
  for (ecma_property_t *cur_prop_p = ecma_get_property_list (obj_p), *prev_prop_p = NULL, *next_prop_p;
       cur_prop_p != NULL;
       prev_prop_p = cur_prop_p, cur_prop_p = next_prop_p)
  {
    next_prop_p = ECMA_GET_POINTER (ecma_property_t,
                                    cur_prop_p->next_property_p);

    if (cur_prop_p == prop_p)
    {
      ecma_free_property (obj_p, prop_p);

      if (prev_prop_p == NULL)
      {
        ecma_set_property_list (obj_p, next_prop_p);
      }
      else
      {
        ECMA_SET_POINTER (prev_prop_p->next_property_p, next_prop_p);
      }

      return;
    }
  }

  JERRY_UNREACHABLE ();
} /* ecma_delete_property */

/**
 * Check that
 */
static void
ecma_assert_object_contains_the_property (const ecma_object_t *object_p, /**< ecma-object */
                                          const ecma_property_t *prop_p) /**< ecma-property */
{
#ifndef JERRY_NDEBUG
  ecma_property_t *prop_iter_p;
  for (prop_iter_p = ecma_get_property_list (object_p);
       prop_iter_p != NULL;
       prop_iter_p = ECMA_GET_POINTER (ecma_property_t, prop_iter_p->next_property_p))
  {
    if (prop_iter_p == prop_p)
    {
      break;
    }
  }

  JERRY_ASSERT (prop_iter_p != NULL);
#else /* JERRY_NDEBUG */
  (void) object_p;
  (void) prop_p;
#endif /* JERRY_NDEBUG */
} /* ecma_assert_object_contains_the_property */

JERRY_STATIC_ASSERT (ECMA_VALUE_SIZE <= 24,
                     maximum_ECMA_VALUE_SIZE_must_be_less_than_or_equal_to_24);

/**
 * Get value field of named data property
 *
 * @return ecma value
 */
inline ecma_value_t __attr_always_inline___
ecma_get_named_data_property_value (const ecma_property_t *prop_p) /**< property */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA);

  ecma_value_t upper_bits = prop_p->h.named_data_property_value_high;
  return (upper_bits << 16) | (prop_p->v.named_data_property.value_low);
} /* ecma_get_named_data_property_value */

/**
 * Set value field of named data property
 */
inline void __attr_always_inline___
ecma_set_named_data_property_value (ecma_property_t *prop_p, /**< property */
                                    ecma_value_t value) /**< value to set */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA);

  prop_p->h.named_data_property_value_high = (uint8_t) (value >> 16);
  prop_p->v.named_data_property.value_low = (uint16_t) value;
} /* ecma_set_named_data_property_value */

/**
 * Assign value to named data property
 *
 * Note:
 *      value previously stored in the property is freed
 */
void
ecma_named_data_property_assign_value (ecma_object_t *obj_p, /**< object */
                                       ecma_property_t *prop_p, /**< property */
                                       ecma_value_t value) /**< value to assign */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA);
  ecma_assert_object_contains_the_property (obj_p, prop_p);

  if (ecma_is_value_number (value)
      && ecma_is_value_number (ecma_get_named_data_property_value (prop_p)))
  {
    const ecma_number_t *num_src_p = ecma_get_number_from_value (value);
    ecma_number_t *num_dst_p = ecma_get_number_from_value (ecma_get_named_data_property_value (prop_p));

    *num_dst_p = *num_src_p;
  }
  else
  {
    ecma_value_t v = ecma_get_named_data_property_value (prop_p);
    ecma_free_value_if_not_object (v);

    ecma_set_named_data_property_value (prop_p, ecma_copy_value_if_not_object (value));
  }
} /* ecma_named_data_property_assign_value */

/**
 * Get getter of named accessor property
 *
 * @return pointer to object - getter of the property
 */
ecma_object_t *
ecma_get_named_accessor_property_getter (const ecma_property_t *prop_p) /**< named accessor property */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR);

  ecma_getter_setter_pointers_t *getter_setter_pointers_p;
  getter_setter_pointers_p = ECMA_GET_POINTER (ecma_getter_setter_pointers_t,
                                               prop_p->v.named_accessor_property.getter_setter_pair_cp);

  return ECMA_GET_POINTER (ecma_object_t, getter_setter_pointers_p->getter_p);
} /* ecma_get_named_accessor_property_getter */

/**
 * Get setter of named accessor property
 *
 * @return pointer to object - setter of the property
 */
ecma_object_t *
ecma_get_named_accessor_property_setter (const ecma_property_t *prop_p) /**< named accessor property */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR);

  ecma_getter_setter_pointers_t *getter_setter_pointers_p;
  getter_setter_pointers_p = ECMA_GET_POINTER (ecma_getter_setter_pointers_t,
                                               prop_p->v.named_accessor_property.getter_setter_pair_cp);

  return ECMA_GET_POINTER (ecma_object_t, getter_setter_pointers_p->setter_p);
} /* ecma_get_named_accessor_property_setter */

/**
 * Set getter of named accessor property
 */
void
ecma_set_named_accessor_property_getter (ecma_object_t *object_p, /**< the property's container */
                                         ecma_property_t *prop_p, /**< named accessor property */
                                         ecma_object_t *getter_p) /**< getter object */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR);
  ecma_assert_object_contains_the_property (object_p, prop_p);

  ecma_getter_setter_pointers_t *getter_setter_pointers_p;
  getter_setter_pointers_p = ECMA_GET_POINTER (ecma_getter_setter_pointers_t,
                                               prop_p->v.named_accessor_property.getter_setter_pair_cp);

  ECMA_SET_POINTER (getter_setter_pointers_p->getter_p, getter_p);
} /* ecma_set_named_accessor_property_getter */

/**
 * Set setter of named accessor property
 */
void
ecma_set_named_accessor_property_setter (ecma_object_t *object_p, /**< the property's container */
                                         ecma_property_t *prop_p, /**< named accessor property */
                                         ecma_object_t *setter_p) /**< setter object */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR);
  ecma_assert_object_contains_the_property (object_p, prop_p);

  ecma_getter_setter_pointers_t *getter_setter_pointers_p;
  getter_setter_pointers_p = ECMA_GET_POINTER (ecma_getter_setter_pointers_t,
                                               prop_p->v.named_accessor_property.getter_setter_pair_cp);

  ECMA_SET_POINTER (getter_setter_pointers_p->setter_p, setter_p);
} /* ecma_set_named_accessor_property_setter */

/**
 * Get property's 'Writable' attribute value
 *
 * @return true - property is writable,
 *         false - otherwise.
 */
inline bool __attr_always_inline___
ecma_is_property_writable (ecma_property_t *prop_p) /**< property */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA);

  return (prop_p->flags & ECMA_PROPERTY_FLAG_WRITABLE) != 0;
} /* ecma_is_property_writable */

/**
 * Set property's 'Writable' attribute value
 */
void
ecma_set_property_writable_attr (ecma_property_t *prop_p, /**< property */
                                 bool is_writable) /**< should the property
                                                    *  be writable? */
{
  JERRY_ASSERT (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA);

  if (is_writable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_WRITABLE);
  }
  else
  {
    prop_p->flags = (uint8_t) (prop_p->flags & ~ECMA_PROPERTY_FLAG_WRITABLE);
  }
} /* ecma_set_property_writable_attr */

/**
 * Get property's 'Enumerable' attribute value
 *
 * @return true - property is enumerable,
 *         false - otherwise.
 */
inline bool __attr_always_inline___
ecma_is_property_enumerable (ecma_property_t *prop_p) /**< property */
{
  JERRY_ASSERT (prop_p->flags & (ECMA_PROPERTY_FLAG_NAMEDDATA | ECMA_PROPERTY_FLAG_NAMEDACCESSOR));

  return (prop_p->flags & ECMA_PROPERTY_FLAG_ENUMERABLE) != 0;
} /* ecma_is_property_enumerable */

/**
 * Set property's 'Enumerable' attribute value
 */
void
ecma_set_property_enumerable_attr (ecma_property_t *prop_p, /**< property */
                                   bool is_enumerable) /**< should the property
                                                        *  be enumerable? */
{
  JERRY_ASSERT (prop_p->flags & (ECMA_PROPERTY_FLAG_NAMEDDATA | ECMA_PROPERTY_FLAG_NAMEDACCESSOR));

  if (is_enumerable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_ENUMERABLE);
  }
  else
  {
    prop_p->flags = (uint8_t) (prop_p->flags & ~ECMA_PROPERTY_FLAG_ENUMERABLE);
  }
} /* ecma_set_property_enumerable_attr */

/**
 * Get property's 'Configurable' attribute value
 *
 * @return true - property is configurable,
 *         false - otherwise.
 */
inline bool __attr_always_inline___
ecma_is_property_configurable (ecma_property_t *prop_p) /**< property */
{
  JERRY_ASSERT (prop_p->flags & (ECMA_PROPERTY_FLAG_NAMEDDATA | ECMA_PROPERTY_FLAG_NAMEDACCESSOR));

  return (prop_p->flags & ECMA_PROPERTY_FLAG_CONFIGURABLE) != 0;
} /* ecma_is_property_configurable */

/**
 * Set property's 'Configurable' attribute value
 */
void
ecma_set_property_configurable_attr (ecma_property_t *prop_p, /**< property */
                                     bool is_configurable) /**< should the property
                                                            *  be configurable? */
{
  JERRY_ASSERT (prop_p->flags & (ECMA_PROPERTY_FLAG_NAMEDDATA | ECMA_PROPERTY_FLAG_NAMEDACCESSOR));

  if (is_configurable)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_CONFIGURABLE);
  }
  else
  {
    prop_p->flags = (uint8_t) (prop_p->flags & ~ECMA_PROPERTY_FLAG_CONFIGURABLE);
  }
} /* ecma_set_property_configurable_attr */

/**
 * Check whether the property is registered in LCache
 *
 * @return true / false
 */
inline bool __attr_always_inline___
ecma_is_property_lcached (ecma_property_t *prop_p) /**< property */
{
  JERRY_ASSERT (prop_p->flags & (ECMA_PROPERTY_FLAG_NAMEDDATA | ECMA_PROPERTY_FLAG_NAMEDACCESSOR));

  return (prop_p->flags & ECMA_PROPERTY_FLAG_LCACHED) != 0;
} /* ecma_is_property_lcached */

/**
 * Set value of flag indicating whether the property is registered in LCache
 */
void
ecma_set_property_lcached (ecma_property_t *prop_p, /**< property */
                           bool is_lcached) /**< contained (true) or not (false) */
{
  JERRY_ASSERT (prop_p->flags & (ECMA_PROPERTY_FLAG_NAMEDDATA | ECMA_PROPERTY_FLAG_NAMEDACCESSOR));

  if (is_lcached)
  {
    prop_p->flags = (uint8_t) (prop_p->flags | ECMA_PROPERTY_FLAG_LCACHED);
  }
  else
  {
    prop_p->flags = (uint8_t) (prop_p->flags & ~ECMA_PROPERTY_FLAG_LCACHED);
  }
} /* ecma_set_property_lcached */

/**
 * Construct empty property descriptor, i.e.:
 *  property descriptor with all is_defined flags set to false and the rest - to default value.
 */
ecma_property_descriptor_t
ecma_make_empty_property_descriptor (void)
{
  ecma_property_descriptor_t prop_desc;

  prop_desc.is_value_defined = false;
  prop_desc.value = ecma_make_simple_value (ECMA_SIMPLE_VALUE_UNDEFINED);
  prop_desc.is_writable_defined = false;
  prop_desc.is_writable = false;
  prop_desc.is_enumerable_defined = false;
  prop_desc.is_enumerable = false;
  prop_desc.is_configurable_defined = false;
  prop_desc.is_configurable = false;
  prop_desc.is_get_defined = false;
  prop_desc.get_p = NULL;
  prop_desc.is_set_defined = false;
  prop_desc.set_p = NULL;

  return prop_desc;
} /* ecma_make_empty_property_descriptor */

/**
 * Free values contained in the property descriptor
 * and make it empty property descriptor
 */
void
ecma_free_property_descriptor (ecma_property_descriptor_t *prop_desc_p) /**< property descriptor */
{
  if (prop_desc_p->is_value_defined)
  {
    ecma_free_value (prop_desc_p->value);
  }

  if (prop_desc_p->is_get_defined
      && prop_desc_p->get_p != NULL)
  {
    ecma_deref_object (prop_desc_p->get_p);
  }

  if (prop_desc_p->is_set_defined
      && prop_desc_p->set_p != NULL)
  {
    ecma_deref_object (prop_desc_p->set_p);
  }

  *prop_desc_p = ecma_make_empty_property_descriptor ();
} /* ecma_free_property_descriptor */

/**
 * Construct property descriptor from specified property
 *
 * @return property descriptor, corresponding to type and content of the specified property, i.e.:
 *                  - for named data properties: { [Value], [Writable], [Enumerable], [Configurable] };
 *                  - for named accessor properties: { [Get] - if defined,
 *                                                     [Set] - if defined,
 *                                                     [Enumerable], [Configurable]
 *                                                   }.
 */
ecma_property_descriptor_t
ecma_get_property_descriptor_from_property (ecma_property_t *prop_p) /**< property */
{
  ecma_property_descriptor_t prop_desc = ecma_make_empty_property_descriptor ();

  prop_desc.is_enumerable = ecma_is_property_enumerable (prop_p);
  prop_desc.is_enumerable_defined = true;
  prop_desc.is_configurable = ecma_is_property_configurable (prop_p);
  prop_desc.is_configurable_defined = true;

  if (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDDATA)
  {
    prop_desc.value = ecma_copy_value (ecma_get_named_data_property_value (prop_p));
    prop_desc.is_value_defined = true;
    prop_desc.is_writable = ecma_is_property_writable (prop_p);
    prop_desc.is_writable_defined = true;
  }
  else if (prop_p->flags & ECMA_PROPERTY_FLAG_NAMEDACCESSOR)
  {
    prop_desc.get_p = ecma_get_named_accessor_property_getter (prop_p);
    prop_desc.is_get_defined = true;
    if (prop_desc.get_p != NULL)
    {
      ecma_ref_object (prop_desc.get_p);
    }

    prop_desc.set_p = ecma_get_named_accessor_property_setter (prop_p);
    prop_desc.is_set_defined = true;
    if (prop_desc.set_p != NULL)
    {
      ecma_ref_object (prop_desc.set_p);
    }
  }

  return prop_desc;
} /* ecma_get_property_descriptor_from_property */

/**
 * Increase reference counter of Compact
 * Byte Code or regexp byte code.
 */
void
ecma_bytecode_ref (ecma_compiled_code_t *bytecode_p) /**< byte code pointer */
{
  /* Abort program if maximum reference number is reached. */
  if (bytecode_p->refs >= UINT16_MAX)
  {
    jerry_fatal (ERR_REF_COUNT_LIMIT);
  }

  bytecode_p->refs++;
} /* ecma_bytecode_ref */

/**
 * Decrease reference counter of Compact
 * Byte Code or regexp byte code.
 */
void
ecma_bytecode_deref (ecma_compiled_code_t *bytecode_p) /**< byte code pointer */
{
  JERRY_ASSERT (bytecode_p->refs > 0);

  bytecode_p->refs--;

  if (bytecode_p->refs > 0)
  {
    /* Non-zero reference counter. */
    return;
  }

  if (bytecode_p->status_flags & CBC_CODE_FLAGS_FUNCTION)
  {
    lit_cpointer_t *literal_start_p = NULL;
    uint32_t literal_end;
    uint32_t const_literal_end;

    if (bytecode_p->status_flags & CBC_CODE_FLAGS_UINT16_ARGUMENTS)
    {
      uint8_t *byte_p = (uint8_t *) bytecode_p;
      literal_start_p = (lit_cpointer_t *) (byte_p + sizeof (cbc_uint16_arguments_t));

      cbc_uint16_arguments_t *args_p = (cbc_uint16_arguments_t *) bytecode_p;
      literal_end = args_p->literal_end;
      const_literal_end = args_p->const_literal_end;
    }
    else
    {
      uint8_t *byte_p = (uint8_t *) bytecode_p;
      literal_start_p = (lit_cpointer_t *) (byte_p + sizeof (cbc_uint8_arguments_t));

      cbc_uint8_arguments_t *args_p = (cbc_uint8_arguments_t *) bytecode_p;
      literal_end = args_p->literal_end;
      const_literal_end = args_p->const_literal_end;
    }

    for (uint32_t i = const_literal_end; i < literal_end; i++)
    {
      mem_cpointer_t bytecode_cpointer = literal_start_p[i];
      ecma_compiled_code_t *bytecode_literal_p = ECMA_GET_NON_NULL_POINTER (ecma_compiled_code_t,
                                                                            bytecode_cpointer);

      /* Self references are ignored. */
      if (bytecode_literal_p != bytecode_p)
      {
        ecma_bytecode_deref (bytecode_literal_p);
      }
    }
  }
  else
  {
#ifndef CONFIG_ECMA_COMPACT_PROFILE_DISABLE_REGEXP_BUILTIN
    re_compiled_code_t *re_bytecode_p = (re_compiled_code_t *) bytecode_p;

    ecma_deref_ecma_string (ECMA_GET_NON_NULL_POINTER (ecma_string_t, re_bytecode_p->pattern_cp));
#endif /* !CONFIG_ECMA_COMPACT_PROFILE_DISABLE_REGEXP_BUILTIN */
  }

  mem_heap_free_block (bytecode_p,
                       ((size_t) bytecode_p->size) << MEM_ALIGNMENT_LOG);
} /* ecma_bytecode_deref */

/**
 * @}
 * @}
 */