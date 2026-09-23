#ifndef ASTRA_USERSPACE_DYNAMIC_LOADER_H
#define ASTRA_USERSPACE_DYNAMIC_LOADER_H

/** @file dynamic_loader.h
 * @brief Validated eager ELF32/m68k dynamic-linking primitives.
 *
 * These routines contain no filesystem or package policy.  The process
 * loader supplies an exact dependency closure, then uses this interface to
 * validate images, resolve versioned symbols, relocate them, and enumerate
 * lifecycle callbacks.
 */

#include <stddef.h>
#include <stdint.h>

typedef enum AstraDynamicStatus {
    ASTRA_DYNAMIC_OK = 0,             /**< Operation completed. */
    ASTRA_DYNAMIC_INVALID_ARGUMENT,   /**< Caller contract was invalid. */
    ASTRA_DYNAMIC_BAD_ELF,            /**< ELF header or segment was invalid. */
    ASTRA_DYNAMIC_BAD_TABLE,          /**< Dynamic metadata was malformed. */
    ASTRA_DYNAMIC_UNSUPPORTED,        /**< Valid feature Astra forbids. */
    ASTRA_DYNAMIC_BAD_STRING,         /**< String-table entry was invalid. */
    ASTRA_DYNAMIC_BAD_SYMBOL,         /**< Symbol-table entry was invalid. */
    ASTRA_DYNAMIC_BAD_RELOCATION,     /**< Relocation record was invalid. */
    ASTRA_DYNAMIC_UNRESOLVED_SYMBOL,  /**< Required symbol was not found. */
    ASTRA_DYNAMIC_VERSION_MISMATCH,   /**< Symbol version was incompatible. */
    ASTRA_DYNAMIC_TLS_UNAVAILABLE     /**< TLS layout was not established. */
} AstraDynamicStatus;

/** A validated mapped dynamic image and its runtime metadata. */
typedef struct AstraDynamicImage {
    uintptr_t mapping_origin;       /**< Host pointer for target address zero. */
    uint32_t mapping_span;          /**< Accessible mapped address span. */
    uint32_t header_address;        /**< Target address of the ELF header. */
    uint32_t load_bias;             /**< Runtime bias applied to symbols. */
    uint32_t program_headers;       /**< Target program-header address. */
    uint32_t interpreter_address;   /**< PT_INTERP target address. */
    uint32_t interpreter_size;      /**< PT_INTERP bytes including NUL. */
    uint32_t dynamic_address;       /**< PT_DYNAMIC target address. */
    uint32_t dynamic_size;          /**< PT_DYNAMIC byte count. */
    uint32_t hash_address;          /**< SysV hash-table target address. */
    uint32_t string_address;        /**< Dynamic string-table address. */
    uint32_t string_size;           /**< Dynamic string-table byte count. */
    uint32_t symbol_address;        /**< Dynamic symbol-table address. */
    uint32_t symbol_count;          /**< Validated dynamic symbol count. */
    uint32_t rela_address;          /**< General RELA-table address. */
    uint32_t rela_size;             /**< General RELA-table bytes. */
    uint32_t jump_rela_address;     /**< PLT RELA-table address. */
    uint32_t jump_rela_size;        /**< PLT RELA-table bytes. */
    uint32_t init_address;          /**< Legacy initializer address. */
    uint32_t fini_address;          /**< Legacy finalizer address. */
    uint32_t init_array_address;    /**< Initializer-array address. */
    uint32_t init_array_size;       /**< Initializer-array bytes. */
    uint32_t fini_array_address;    /**< Finalizer-array address. */
    uint32_t fini_array_size;       /**< Finalizer-array bytes. */
    uint32_t preinit_array_address; /**< Program preinitializer address. */
    uint32_t preinit_array_size;    /**< Program preinitializer bytes. */
    uint32_t version_symbols;       /**< GNU version-symbol table address. */
    uint32_t version_definitions;   /**< GNU version-definition address. */
    uint32_t version_definition_count; /**< Version-definition count. */
    uint32_t version_needs;         /**< GNU version-requirement address. */
    uint32_t version_need_count;    /**< Version-requirement file count. */
    uint32_t relro_address;         /**< GNU RELRO range start. */
    uint32_t relro_size;            /**< GNU RELRO range bytes. */
    uint32_t tls_address;           /**< TLS template target address. */
    uint32_t tls_file_size;         /**< Initialized TLS bytes. */
    uint32_t tls_memory_size;       /**< Total TLS bytes. */
    uint32_t tls_alignment;         /**< Required TLS alignment. */
    int32_t tls_tp_offset;          /**< Assigned thread-pointer offset. */
    uint32_t tls_module;            /**< Assigned TLS module identifier. */
    uint16_t program_header_count;  /**< Validated program-header count. */
    uint16_t elf_type;              /**< ELF ET_EXEC or ET_DYN value. */
    uint32_t needed_count;          /**< DT_NEEDED entry count. */
} AstraDynamicImage;

/** Result of resolving one versioned dynamic symbol. */
typedef struct AstraDynamicResolution {
    uint32_t address;       /**< Runtime address for a non-TLS symbol. */
    uint32_t tls_module;    /**< TLS module identifier. */
    uint32_t tls_offset;    /**< Offset within the module TLS image. */
    int32_t tls_tp_offset;  /**< Module offset from the thread pointer. */
    uint8_t symbol_type;    /**< ELF symbol type. */
} AstraDynamicResolution;

/** Resolve an exact symbol name and optional ABI version. */
typedef AstraDynamicStatus (*AstraDynamicResolver)(
    void *context, const char *name, const char *version,
    AstraDynamicResolution *resolution);

/** Caller-ordered set searched by astra_dynamic_resolve_closure(). */
typedef struct AstraDynamicClosure {
    const AstraDynamicImage *images; /**< Dependency-order image array. */
    uint32_t count;                  /**< Number of array elements. */
} AstraDynamicClosure;

/** Combined initial-exec TLS layout shared by every thread in one process. */
typedef struct AstraDynamicTlsLayout {
    uint32_t memory_size;  /**< Complete zero-initialized template size. */
    uint32_t alignment;    /**< Maximum module alignment, always a power of 2. */
    uint32_t module_count; /**< Number of images that contribute TLS. */
} AstraDynamicTlsLayout;

/**
 * Validate a mapped eager-dynamic ELF image without allocating memory.
 * @param mapping_origin Host pointer corresponding to target address zero.
 * @param mapping_span Bytes accessible from `mapping_origin`.
 * @param header_address Target ELF-header address within the mapping.
 * @param load_bias Runtime address bias applied to dynamic symbols.
 * @param image Receives validated metadata. The mapped ELF metadata must
 * remain unchanged while this image is used.
 * @return Detailed dynamic-loader status.
 */
AstraDynamicStatus astra_dynamic_open(uintptr_t mapping_origin,
                                      uint32_t mapping_span,
                                      uint32_t header_address,
                                      uint32_t load_bias,
                                      AstraDynamicImage *image);

/** Return nonzero when an image range is backed by writable PT_LOAD memory. */
int astra_dynamic_writable_range(const AstraDynamicImage *image,
                                 uint32_t address, uint32_t size);

/** Return one DT_NEEDED identity by dependency-table index. */
AstraDynamicStatus astra_dynamic_needed(
    const AstraDynamicImage *image, uint32_t index, const char **name);

/** Return the exact DT_SONAME identity carried by a shared object. */
AstraDynamicStatus astra_dynamic_soname(
    const AstraDynamicImage *image, const char **name);

/** Return the exact PT_INTERP identity carried by an executable. */
AstraDynamicStatus astra_dynamic_interpreter(
    const AstraDynamicImage *image, const char **name);

/** Resolve a versioned public symbol exported by one image. */
AstraDynamicStatus astra_dynamic_lookup(
    const AstraDynamicImage *image, const char *name, const char *version,
    AstraDynamicResolution *resolution);

/** Resolve through a caller-ordered, already validated dependency closure. */
AstraDynamicStatus astra_dynamic_resolve_closure(
    void *context, const char *name, const char *version,
    AstraDynamicResolution *resolution);

/** Apply all eager RELA records using the supplied exact resolver. */
AstraDynamicStatus astra_dynamic_relocate(
    AstraDynamicImage *image, AstraDynamicResolver resolver, void *context);

/**
 * Assign module identifiers and A4-relative offsets for an image closure.
 *
 * Images must already have passed astra_dynamic_open().  Their order is the
 * caller's stable dependency order and therefore becomes their TLS module
 * order.  Images without PT_TLS consume neither storage nor an identifier.
 *
 * @param images Mutable validated image array.
 * @param count Number of images in `images`.
 * @param layout Receives the exact combined allocation contract.
 * @return ASTRA_DYNAMIC_OK or a validation/overflow status.
 */
AstraDynamicStatus astra_dynamic_tls_layout(
    AstraDynamicImage *images, uint32_t count,
    AstraDynamicTlsLayout *layout);

/**
 * Materialize a previously assigned combined TLS template.
 *
 * The complete output is cleared before initialized PT_TLS bytes are copied,
 * so every module's `.tbss` and all alignment gaps start at zero.  The caller
 * retains ownership of the storage and passes it to Axiom's dynamic-process
 * finalization call.
 *
 * @param images Images previously passed to astra_dynamic_tls_layout().
 * @param count Number of images in `images`.
 * @param layout Layout returned for this exact ordered image set.
 * @param destination Writable output storage, or NULL for an empty layout.
 * @param capacity Bytes available at `destination`.
 * @return ASTRA_DYNAMIC_OK or a validation/buffer status.
 */
AstraDynamicStatus astra_dynamic_tls_initialize(
    const AstraDynamicImage *images, uint32_t count,
    const AstraDynamicTlsLayout *layout, void *destination,
    uint32_t capacity);

/** Return the number of preinitializers in a main executable. */
uint32_t astra_dynamic_preinitializer_count(const AstraDynamicImage *image);
/** Return one preinitializer address by index. */
AstraDynamicStatus astra_dynamic_preinitializer(
    const AstraDynamicImage *image, uint32_t index, uint32_t *address);
/** Return the number of initializers in an image. */
uint32_t astra_dynamic_initializer_count(const AstraDynamicImage *image);
/** Return one initializer address by index. */
AstraDynamicStatus astra_dynamic_initializer(
    const AstraDynamicImage *image, uint32_t index, uint32_t *address);
/** Return the number of finalizers in an image. */
uint32_t astra_dynamic_finalizer_count(const AstraDynamicImage *image);
/** Return one finalizer address by index. */
AstraDynamicStatus astra_dynamic_finalizer(
    const AstraDynamicImage *image, uint32_t index, uint32_t *address);

/** Return stable diagnostic text for a dynamic-loader status. */
const char *astra_dynamic_status_text(AstraDynamicStatus status);

#endif
