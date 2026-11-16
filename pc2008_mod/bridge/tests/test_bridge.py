import copy,hashlib,json,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import build_hulk_pc_bridge as bridge
from map_pc_d3d9_shaders import fnv32,crc32

GROUPS=['Objects','QuickDatas','QDMetadatas','PclEffects','Actions','Skeletons',
        'Models','Havoks','HavokMopps','NavMeshes','Animations','Fonts','Shaders',
        'ShaderEffects','Textures','Sounds','PS2Sounds','Samples','AudioStreams',
        'Binaries','TexStreams','SurfaceDatas','ToneMapCurves','Datasets',
        'FmodProjects','WaveBanks']

def sample_model(name):
    raw=b'\x01\0\0\0\0\0'+name.encode()+b'\0'+b'\0'+struct.pack('<I',1)
    return raw+b'first_surface\0'+(b'\0'*64)

class BridgeTests(unittest.TestCase):
    def test_pc_index_group_roundtrip(self):
        entries=[(name,{}) for name in GROUPS]
        sample=bridge.pack_index(entries)
        self.assertEqual(bridge.parse_index(sample),entries)
        self.assertEqual(bridge.pack_index(bridge.parse_index(sample)),sample)
        self.assertRaises(ValueError,bridge.parse_index,sample[:-2])
    def test_internal_arc_index_and_new_pc_models(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            name='test_eor_bridge_model'
            original=sample_model(name)
            key=bridge.crc_hash(name)
            data=bytearray(b'\0'*4+original)
            originals={key:(4,len(original),0)}
            names={key:(name.encode(),b'\x01'*8)}
            pos=len(data)
            data+=bridge.pack_arc_footer(originals,names,pos)
            struct.pack_into('<I',data,0,pos)
            old_arc=root/'models.arc';old_arc.write_bytes(data)
            original_index=bridge.pack_index([(n,originals if n=='Models' else {}) for n in GROUPS])
            index=root/'index.ind';index.write_bytes(original_index)
            previous_expected=bridge.SHA256_PC_INDEX
            try:
                bridge.SHA256_PC_INDEX=bridge.hash256(original_index)
                newname='test_eor_bridge_extra'
                payload=sample_model(newname)
                payload_file=root/'converted.bin';payload_file.write_bytes(payload)
                record={'resource_hash':f'0x{bridge.crc_hash(newname):08x}',
                        'pc_payload_path':str(payload_file),'sha256':bridge.hash256(payload)}
                manifest=root/'manifest.json';manifest.write_text(json.dumps({
                    'format':'HulkBridgePC-v1','platform':'PC_D3D9',
                    'category':'Models','records':[record]}))
                destination=root/'HulkBridge'
                report=bridge.build(index,old_arc,manifest,destination)
                self.assertEqual(report['virtual_index_entries'],2)
                self.assertEqual(report['records'][0]['kind'],'extra')
                valid=bridge.parse_index((destination/'index.ind').read_bytes())
                modeldict=dict(valid)['Models']
                new_arc=(destination/'data'/'MODELS.ARC').read_bytes()
                offset,names=bridge.parse_arc_footer(new_arc,modeldict)
                self.assertEqual(len(names),2)
                self.assertEqual(names[bridge.crc_hash(newname)][0],newname.encode())
                self.assertEqual(new_arc[4:pos],data[4:pos])
                self.assertEqual((destination/'HulkBridge.map').read_bytes(),
                                 bridge.HEADER+b'index.ind\nMODELS.ARC\n')
                self.assertEqual(new_arc[int.from_bytes(new_arc[:4],'little'):],
                                 bridge.pack_arc_footer(modeldict,names,offset))
                # Refuse raw Xbox endian model records even if checksum matches.
                bad=bytearray(payload);bad[0],bad[1]=0,1
                payload_file.write_bytes(bad)
                record['sha256']=bridge.hash256(bad)
                manifest.write_text(json.dumps({'format':'HulkBridgePC-v1',
                    'platform':'PC_D3D9','category':'Models','records':[record]}))
                with self.assertRaises(ValueError):
                    bridge.build(index,old_arc,manifest,root/'Rejected')
                self.assertFalse((root/'Rejected').exists())
            finally:
                bridge.SHA256_PC_INDEX=previous_expected
    def test_shader_format_refuses_xenos_without_d3d9_tokens(self):
        name=b'Ribbon\0'
        key=bridge.crc_hash('Ribbon')
        vs=struct.pack('<II',0xfffe0200,0x0044fffe)+b'CTAB'+b'\0'*32+b'\xff\xff\0\0'
        ps=struct.pack('<II',0xffff0200,0x0044fffe)+b'CTAB'+b'\0'*32+b'\xff\xff\0\0'
        pc=name+vs+ps+b'\0'*160
        bridge.validate_effect(pc,key)
        with self.assertRaises(ValueError):bridge.validate_effect(b'Xbox'+b'\0'*180,key)
        with self.assertRaises(ValueError):bridge.validate_effect(pc.replace(struct.pack('<I',0xfffe0200),b'\x10\x2a\x11\0'),key)
        self.assertEqual(bridge.crc_hash('Ribbon'),0x01e1c422)
    def test_pc_shader_override_fnv_crc(self):
        known=b'original shader contents\xff\xff\0\0'
        self.assertEqual(fnv32(known),fnv32(known))
        self.assertNotEqual(fnv32(known),fnv32(known+b'1'))
        self.assertNotEqual(crc32(known),crc32(known+b'1'))

if __name__=='__main__':unittest.main()
