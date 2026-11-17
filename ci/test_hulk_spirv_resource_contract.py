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


class NativeImageResourceContract(unittest.TestCase):
    def test_empty_shader_no_resources_is_valid(self):
        self.assertEqual(validate_named_spirv_images(words_to_bytes(HEADER)), 0)

    def test_original_named_sampler_and_binding(self):
        spv = words_to_bytes(HEADER + NAME + BINDINGS + IMAGE)
        self.assertEqual(validate_named_spirv_images(spv), 1)

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
