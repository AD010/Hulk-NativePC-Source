#!/usr/bin/env python3
"""Source-only, no original Hulk shader bytes: native EoR SPIR-V SRV ABI tests."""
import struct
import unittest

from hulk_spirv_resource_contract import validate_named_spirv_images


def words_to_bytes(words):
    return struct.pack("<" + "I" * len(words), *words)


HEADER = [0x07230203, 0x00010500, 0, 16, 0,
          0x00020011, 1, 0x0003000e, 0, 1,
          0x0005000f, 4, 1, 0x6e69616d, 0]
IMAGE = [0x00090019, 2, 5, 1, 0, 0, 0, 1, 0,
         0x00040020, 3, 0, 2, 0x0004003b, 3, 4, 0]
BINDINGS = [0x00040047, 4, 33, 2, 0x00040047, 4, 34, 0]
NAME = [0x00050005, 4, 0x69445f67, 0x73756666, 0x00000065]  # g_Diffuse
BAD_UNRELATED_NAME = [0x00050005, 9, 0x64616220, 0x65707974, 0]  # " badtype"




def standalone_sampler_words(binding=5, descriptor_set=1):
    # OpTypeSampler %6; OpTypePointer %7 UniformConstant %6;
    # OpVariable %7 %8 UniformConstant, as used for separate SamplerState.
    return ([0x0002001a, 6, 0x00040020, 7, 0, 6, 0x0004003b, 7, 8, 0],
            [0x00040047, 8, 33, binding,
             0x00040047, 8, 34, descriptor_set])


def op_name(identifier, name):
    payload = name.encode("ascii") + b"\0"
    payload += b"\0" * (-len(payload) % 4)
    words = list(struct.unpack("<" + "I" * (len(payload) // 4), payload))
    return [((len(words) + 2) << 16) | 5, identifier, *words]

class NativeImageResourceContract(unittest.TestCase):
    def test_empty_shader_no_resources_is_valid(self):
        self.assertEqual(validate_named_spirv_images(words_to_bytes(HEADER)), 0)

    def test_original_named_sampler_and_binding(self):
        spv = words_to_bytes(HEADER + NAME + BINDINGS + IMAGE)
        self.assertEqual(validate_named_spirv_images(spv), 1)


    def test_standalone_xenos_sampler_binding(self):
        # An absent OpName on an original Xenos sampler must fail closed.
        sampler, decorations = standalone_sampler_words()
        self.assertEqual(validate_named_spirv_images(words_to_bytes(
            HEADER + op_name(8, "HulkSampler_s3") + decorations + sampler)), 1)

    def test_missing_sampler_name_fails_before_native_bind(self):
        sampler, decorations = standalone_sampler_words()
        with self.assertRaisesRegex(ValueError, "lost native descriptor/name"):
            validate_named_spirv_images(words_to_bytes(HEADER + decorations + sampler))

    def test_missing_sampler_descriptor_set_fails(self):
        sampler, decorations = standalone_sampler_words()
        with self.assertRaisesRegex(ValueError, "lost native descriptor/name"):
            validate_named_spirv_images(words_to_bytes(
                HEADER + op_name(8, "HulkSampler_s3") + decorations[:4] + sampler))

    def test_sampler_image_binding_collision_fails(self):
        sampler, decorations = standalone_sampler_words(binding=2, descriptor_set=0)
        with self.assertRaisesRegex(ValueError, "Duplicate EoR sampled image"):
            validate_named_spirv_images(words_to_bytes(
                HEADER + op_name(8, "HulkSampler_s3") + NAME +
                decorations + BINDINGS + sampler + IMAGE))

    def test_separate_image_and_sampler_descriptors_both_checked(self):
        sampler, decorations = standalone_sampler_words()
        self.assertEqual(validate_named_spirv_images(words_to_bytes(
            HEADER + op_name(8, "HulkSampler_s3") + NAME +
            decorations + BINDINGS + sampler + IMAGE)), 2)

    def test_missing_sampled_image_name_fails(self):
        with self.assertRaisesRegex(ValueError, "lost native descriptor/name"):
            validate_named_spirv_images(words_to_bytes(HEADER + BINDINGS + IMAGE))

    def test_missing_descriptor_set_fails(self):
        with self.assertRaises(ValueError):
            validate_named_spirv_images(words_to_bytes(HEADER + NAME + BINDINGS[:4] + IMAGE))

    def test_unrelated_debug_name_does_not_poison_resource_reflection(self):
        self.assertEqual(validate_named_spirv_images(
            words_to_bytes(HEADER + BAD_UNRELATED_NAME + NAME + BINDINGS + IMAGE)), 1)

    def test_sampled_image_symbol_must_not_be_unsafe(self):
        odd = [0x00040005, 4, 0x20616220, 0x00000063]
        with self.assertRaises(ValueError):
            validate_named_spirv_images(words_to_bytes(HEADER + odd + BINDINGS + IMAGE))

    def test_reject_hidden_resource_name_suffix(self):
        bad_name = NAME.copy()
        bad_name[-1] |= 0x78000000
        with self.assertRaisesRegex(ValueError, "padding"):
            validate_named_spirv_images(words_to_bytes(
                HEADER + bad_name + BINDINGS + IMAGE))

    def test_reject_array_descriptor_before_runtime(self):
        # Array of image types is unsupported by the single-SRV binder.
        image = IMAGE[:9] + [0x0004001c, 6, 2, 7,
                            0x00040020, 3, 0, 6, 0x0004003b, 3, 4, 0]
        with self.assertRaisesRegex(ValueError, "array"):
            validate_named_spirv_images(words_to_bytes(
                HEADER + NAME + BINDINGS + image))

    def test_truncated_image_type_fails(self):
        malformed = [0x00020019, 2] + IMAGE[9:]
        with self.assertRaisesRegex(ValueError, "declaration length"):
            validate_named_spirv_images(words_to_bytes(
                HEADER + NAME + BINDINGS + malformed))

    def test_duplicate_variable_id_fails(self):
        with self.assertRaisesRegex(ValueError, "duplicate SPIR-V variable"):
            validate_named_spirv_images(words_to_bytes(
                HEADER + NAME + BINDINGS + IMAGE + IMAGE[-4:]))

    def test_extra_descriptor_operand_fails(self):
        with self.assertRaisesRegex(ValueError, "decoration length"):
            validate_named_spirv_images(words_to_bytes(
                HEADER + NAME + IMAGE + [0x00050047, 4, 33, 2, 0] + BINDINGS[4:]))

    def test_invalid_instruction_extent_fails(self):
        with self.assertRaises(ValueError):
            validate_named_spirv_images(words_to_bytes(HEADER + [0]))

    def test_invalid_image_descriptor_collision_fails(self):
        extra = [0x00040020, 7, 0, 2, 0x0004003b, 7, 8, 0,
                 0x00040005, 8, 0x6f635f67, 0,
                 0x00040047, 8, 33, 2,
                 0x00040047, 8, 34, 0]
        with self.assertRaises(ValueError):
            validate_named_spirv_images(words_to_bytes(
                HEADER + NAME + BINDINGS + IMAGE + extra))


if __name__ == "__main__":
    unittest.main()
