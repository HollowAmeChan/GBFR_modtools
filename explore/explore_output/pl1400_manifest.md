# pl1400 角色资源清单

| 字段 | 値 |
|---|---|
| **生成时间** | 2026-07-15 04:04:44 |
| **角色ID** | pl1400 |
| **数字ID** | 1400 |
| **前缀类型** | pl -- 玩家角色 |
| **游戏根目录** | D:\Steam\steamapps\common\Granblue Fantasy Relink |

---

## 模型层

### 主体模型   `pl/pl1400/`

| 文件 | 大小 | 说明 |
|------|------|-------------|
| `pl1400.minfo` | 8.3 KB | 模型结构入口 — 描述 mesh 分段、材质槽、骨架绑定 |
| `pl1400.mmesh` | 3.2 MB | 本地网格缓存（同时存在于 model_streaming/） |
| `pl1400.skeleton` | 22.8 KB | 骨架定义 — 骨骼名称、层级、绑定姿势 (T-Pose) |
| `pl1400.sop` | 16.1 KB | 形态/遮挡/物理附加数据 (Shape/Occlusion/Physics) |
| `vars/0.mmat` | 6.2 KB | 材质变体 0 -- 默认服装 |
| `vars/1.mmat` | 6.2 KB | 材质变体 1 -- 变体 1 |
| `vars/10.mmat` | 6.4 KB | 材质变体 10 -- 变体 10 |
| `vars/2.mmat` | 6.2 KB | 材质变体 2 -- 变体 2 |
| `vars/3.mmat` | 6.2 KB | 材质变体 3 -- 变体 3 |
| `vars/4.mmat` | 6.2 KB | 材质变体 4 -- 变体 4 |
| `vars/5.mmat` | 6.2 KB | 材质变体 5 -- 变体 5 |
| `vars/6.mmat` | 6.2 KB | 材质变体 6 -- 变体 6 |
| `vars/7.mmat` | 6.2 KB | 材质变体 7 -- 变体 7 |
| `vars/8.mmat` | 6.2 KB | 材质变体 8 -- 变体 8 |
| `vars/9.mmat` | 6.2 KB | 材质变体 9 -- 变体 9 |

#### 贴图引用（来自 vars/*.mmat）

| 哈希(前16位) | 完整哈希 | 贴图名 | 来源mmat |
|---|---|---|---|
| `4cbcc575cba2b99f` | `4cbcc575cba2b99f7768ee3c7bd6f1ec799dac5a1be19604a50223018a46c3fc` | `pl1400_skin_lod0_nrml` | `0.mmat` |
| `05ccbc22d79e5da8` | `05ccbc22d79e5da8dfbc166244a0a8fe23d01882b69e59d7c5cff31f514b8b3e` | `pl1400_sheath_lod0_nrml` | `0.mmat` |
| `72c20afbcb18214e` | `72c20afbcb18214ecd66865ea6e4fd5dcb864a4f5a75a0bdb075a0210d0091b3` | `pl1400_sheath05_lod0_nrml` | `0.mmat` |
| `687ea893af62d9fc` | `687ea893af62d9fc8e4e264722a5f341d7a34ba2b1fc0d5e48ea1a40bbadb657` | `pl1400_sheath04_lod0_nrml` | `0.mmat` |
| `cc9f6e0e891a3efe` | `cc9f6e0e891a3efef5d832642a3b2130077d09cd87734a7e8fc8f5f70a4a7bfa` | `pl1400_hair02_lod0_nrml` | `0.mmat` |
| `c7b1848cdd4b1f45` | `c7b1848cdd4b1f453808480ade9485775c9cfa9fdf7aceb8c7e96e79c6d8e923` | `pl1400_hair01_lod0_nrml` | `0.mmat` |
| `996aaa772426bf3d` | `996aaa772426bf3dd97987bad81f53d2b86181aa675f76e84831902f78c02b9c` | `pl1400_body03_lod0_nrml` | `0.mmat` |
| `2216dc65666f2c11` | `2216dc65666f2c11dd73a0d9962c855b86352488b39e8cebc39033156f872b0b` | `pl1400_body02_lod0_nrml` | `0.mmat` |
| `7c0b2114adb11aba` | `7c0b2114adb11abaf0a989ac5028b0d1f4f1d207a84e2b756691956628da449a` | `pl1400_body01_lod0_nrml` | `0.mmat` |
| `54053fca897e232d` | `54053fca897e232dcfa2c5b02aa7a64120aa77c3cae27d6d0de0df1bb347f22f` | `pl1400_sheath_lod0_nrml` | `1.mmat` |
| `6bede3eb51669db4` | `6bede3eb51669db4c353117c6eadd59702f6daf89598002d27d4e22d56409ecb` | `pl1400_hair02_lod0_nrml` | `1.mmat` |
| `b8ead3a6f6b64c14` | `b8ead3a6f6b64c14955af89a2d9f5cc7734dc660574a46b80c72e62741dfd6dc` | `pl1400_hair01_lod0_nrml` | `1.mmat` |
| `79d560a2500348f6` | `79d560a2500348f67d97950a6289e2bde58ccc45a6150a0d1792e68051707131` | `pl1400_body03_lod0_nrml` | `1.mmat` |
| `b9e3541526a8353c` | `b9e3541526a8353c4b5361700ee50439abebe832fcd92956830a72d082cb5703` | `pl1400_body02_lod0_nrml` | `1.mmat` |
| `53090a5acbf8348c` | `53090a5acbf8348c48d66776654d0250e59f961ea786f98caca55e28a68a7c0f` | `pl1400_body01_lod0_nrml` | `1.mmat` |
| `0c93c27f08dd5eef` | `0c93c27f08dd5eef2fb7ac61a8435846031f1ea8de39ccdadd58def9ec3aec93` | `pl1400_sheath_lod0_nrml` | `10.mmat` |
| `ce3ad3fbcfbb2cef` | `ce3ad3fbcfbb2cef9e82f5f0f9902e69c2871c6301229b72cb16db72cab63309` | `pl1400_body03_lod0_nrml` | `10.mmat` |
| `ddc23624d9a08229` | `ddc23624d9a08229bbf2fe3e983fe26f3e5c2333bb30c6106fdf01671902ae5b` | `pl1400_body02_lod0_nrml` | `10.mmat` |
| `78ce10697e37e402` | `78ce10697e37e402aa4e85a450fa0b1c41bd9406e3ffb4207543ff07e023bdda` | `pl1400_body01_lod0_nrml` | `10.mmat` |
| `970fc7064d81ff40` | `970fc7064d81ff4004847d9010a2bdeaa99361ecd6bb27806e7deaa835842a83` | `pl1400_sheath_lod0_nrml` | `2.mmat` |
| `26bfcfd45ee7a699` | `26bfcfd45ee7a699e6491d13b458d6d91c0f6804725170c45588735db386f2fc` | `pl1400_hair02_lod0_nrml` | `2.mmat` |
| `52c38316f60016cf` | `52c38316f60016cf79b4d747edefeb8328ddfa11d93694ebd57a8db4dd6cdf2a` | `pl1400_hair01_lod0_nrml` | `2.mmat` |
| `6c04f54d9841a058` | `6c04f54d9841a0587026a8ce4b0b31d41f7a914fe4169d0c093f5b4065bc7b38` | `pl1400_body03_lod0_nrml` | `2.mmat` |
| `c82ee86c525129f3` | `c82ee86c525129f3ace72fdb133d78cf0da346ca6e0c4692bf7f9055bb41cc71` | `pl1400_body02_lod0_nrml` | `2.mmat` |
| `081dd0d73c453708` | `081dd0d73c4537085b37db046f53435cf7e8bf4cec84945f33474a26a451e2ab` | `pl1400_body01_lod0_nrml` | `2.mmat` |
| `9e1f74ca08eec081` | `9e1f74ca08eec08162d9d4099f0a87680509e5dea20c14ce43a8e74b50f04f44` | `pl1400_sheath_lod0_nrml` | `3.mmat` |
| `abf9ea46b9aaf6b8` | `abf9ea46b9aaf6b848c91079aa696d186398641a7d59b137a33d870f52f7e42d` | `pl1400_hair02_lod0_nrml` | `3.mmat` |
| `7d4d946c3af4aace` | `7d4d946c3af4aace5148cc7d9476d37a00b7defe592591561149b135ee6442f6` | `pl1400_hair01_lod0_nrml` | `3.mmat` |
| `0c9aa1b6a57e68e0` | `0c9aa1b6a57e68e06a721b04d633e4587523ac5ce85d1b3a9eefe9f45339a240` | `pl1400_body03_lod0_nrml` | `3.mmat` |
| `bbe5098ffadfb853` | `bbe5098ffadfb853acbdf01ee2deae614c96d45ad2645648c53a7b8d306f0e3f` | `pl1400_body02_lod0_nrml` | `3.mmat` |
| `bc16166cfe14e4a4` | `bc16166cfe14e4a4f8338c711a1c4c8bf455024a629fb2ecddb74223b2eae807` | `pl1400_body01_lod0_nrml` | `3.mmat` |
| `e4bd438fc956e38b` | `e4bd438fc956e38b199248f7d9a8c2b06e6882cfadaaf1e70d496801be6792ba` | `pl1400_sheath_lod0_nrml` | `4.mmat` |
| `ec6daf9e608104ed` | `ec6daf9e608104ed04820a7e1d5bba523b5ded3bcfb1654bc628ed3532f59f2e` | `pl1400_hair02_lod0_nrml` | `4.mmat` |
| `fad7834b24371263` | `fad7834b243712632c4763350b5bb99049c0a10224fbb3c3026deb33af4af87a` | `pl1400_hair01_lod0_nrml` | `4.mmat` |
| `c27bb696c9b46525` | `c27bb696c9b46525ff026292b1576870ba1c488c8798ede5ccf6419844d14a41` | `pl1400_body03_lod0_nrml` | `4.mmat` |
| `86ceff21543beca0` | `86ceff21543beca0c49d2ce70d300c11552d651f5f4c02afae432b3f1ce94620` | `pl1400_body02_lod0_nrml` | `4.mmat` |
| `d9d84003b4ad05a1` | `d9d84003b4ad05a18fb2e55ddd11d42b7ca67899bd8f40991f2ff089e5919712` | `pl1400_body01_lod0_nrml` | `4.mmat` |
| `efb3a02d04c150e1` | `efb3a02d04c150e157eee4c8d60362fdec129a1a2124f09792f8d076e24e5925` | `pl1400_sheath_lod0_nrml` | `5.mmat` |
| `4bc36a2e98496e45` | `4bc36a2e98496e4592070d12b6377382390315f739f65ad8604cb6d2d9d92a4d` | `pl1400_hair02_lod0_nrml` | `5.mmat` |
| `681df04157bf32f3` | `681df04157bf32f3ea6915ec1902ad7ea8e829951b013b652980a947b84f8c43` | `pl1400_hair01_lod0_nrml` | `5.mmat` |
| `6395c0e90109b442` | `6395c0e90109b4427916e88d991d00c2e80920d67aee8262243064050670bf8e` | `pl1400_body03_lod0_nrml` | `5.mmat` |
| `f3a158175dd78f83` | `f3a158175dd78f836f4764d5fa6b52cac61a67be6d46c69ec4ff3e9e6d56aa03` | `pl1400_body02_lod0_nrml` | `5.mmat` |
| `0822b391682d4d53` | `0822b391682d4d53972dd0895ca30fdcf9db0820d15782af9877364c9b7a56c6` | `pl1400_body01_lod0_nrml` | `5.mmat` |
| `00c12cb91b7f3890` | `00c12cb91b7f38902539421986b5c2744032dfbd2e0c60df855b3dad6725d918` | `pl1400_sheath_lod0_nrml` | `6.mmat` |
| `3768894d936240b1` | `3768894d936240b197aefaa9d9054d0bf3c9c928aca3349f2be68ca1e29e0625` | `pl1400_hair02_lod0_nrml` | `6.mmat` |
| `025cbe12a4713861` | `025cbe12a4713861f118527915015ccc59d60fcf56355eef13dc439bb3e22aea` | `pl1400_hair01_lod0_nrml` | `6.mmat` |
| `15a405e08053ee29` | `15a405e08053ee295fcfce682eb56600a02e14549dbce0c0a220d8b340069dfb` | `pl1400_body03_lod0_nrml` | `6.mmat` |
| `f914f7b2a816fec6` | `f914f7b2a816fec680796016cf56522286548347a1dac2849aacab7bd8c5c2fe` | `pl1400_body02_lod0_nrml` | `6.mmat` |
| `bff883e31274b78c` | `bff883e31274b78c3b6eb5744d1aa113e5d37f177cdb25ebed53d726797ff64e` | `pl1400_body01_lod0_nrml` | `6.mmat` |
| `4ffb8f4e46f7e9a1` | `4ffb8f4e46f7e9a157c29f634fa8a0fed52468d38ac11688cb87b729294700ed` | `pl1400_sheath_lod0_nrml` | `7.mmat` |
| `56fdf9fee90ef137` | `56fdf9fee90ef1378ffbbe9ca9739f2d892d143ea1bb2e0a4aef1e77fbdfafa6` | `pl1400_hair02_lod0_nrml` | `7.mmat` |
| `680e4632c872679a` | `680e4632c872679aa7a2c780077fe09523f646f371f9ef6a4e460621076e7130` | `pl1400_hair01_lod0_nrml` | `7.mmat` |
| `3e06d66a2405cba6` | `3e06d66a2405cba6246be020c32f003e3390a44a2eecf2850eaeaa3fcf566542` | `pl1400_body03_lod0_nrml` | `7.mmat` |
| `f38986a58c1ad12f` | `f38986a58c1ad12f014d5a1e9d1f2bc24760fdd073bc960610ef342eb5971895` | `pl1400_body02_lod0_nrml` | `7.mmat` |
| `14563139581bedf0` | `14563139581bedf0d6fc0cf4790a4ea026dbe7352b4e1983224e97861bc46b53` | `pl1400_body01_lod0_nrml` | `7.mmat` |
| `084ec5420f3769c5` | `084ec5420f3769c500ce9a10265a6647c27580988756187832e98eedd05fb7dc` | `pl1400_sheath_lod0_nrml` | `8.mmat` |
| `33572f204080a9d4` | `33572f204080a9d40ffc7ed4f155f403f7bf2c8ff3ffba2ef667199c22f59f10` | `pl1400_body03_lod0_nrml` | `8.mmat` |
| `2ba0be1bb726c6a4` | `2ba0be1bb726c6a4710af7977916a5057a0cbdb9b5c9c64f0fb21d7a4d71ad2a` | `pl1400_body02_lod0_nrml` | `8.mmat` |
| `43f0608f2dbd9c3e` | `43f0608f2dbd9c3ed2e9b47752ddd24498eab0f14b11fc83b61ebdd86a032d3a` | `pl1400_body01_lod0_nrml` | `8.mmat` |
| `5dd6e705c11c99be` | `5dd6e705c11c99be713b1d5e13861a8c9390e3eae0c4ae6d80abe7701e434bc4` | `pl1400_sheath_lod0_nrml` | `9.mmat` |
| `d8b9e9796bbd3aa1` | `d8b9e9796bbd3aa18028bcbafe345663a0e71034539cedd9e076ad95ec39e52b` | `pl1400_hair02_lod0_nrml` | `9.mmat` |
| `457356a74e3e860a` | `457356a74e3e860a2d51463ecfa7c25fdc43f337b5a59eabd44e2175de9d222b` | `pl1400_hair01_lod0_nrml` | `9.mmat` |
| `06296cdaf9346159` | `06296cdaf9346159c353f2a4c53bb1eea4afa16175ee2251758b2262a8289ab6` | `pl1400_body03_lod0_nrml` | `9.mmat` |
| `f6b449d66ecea91b` | `f6b449d66ecea91b1ed6fbc9e34d1d08ce84f7ba733f8e529417b140a6533822` | `pl1400_body02_lod0_nrml` | `9.mmat` |
| `63d1e3b9e2dc9542` | `63d1e3b9e2dc9542cd773463eb90fd52e7da0fe223932b169470fbbdad9ebdb8` | `pl1400_body01_lod0_nrml` | `9.mmat` |

> **提取说明：这些哈希指向 Granite VT 图块（albd/nrml/msk1-2）。**
> GraniteTextureReader 版本须与游戏构建匹配——游戏更新后部分图块可能
> 返回 0 个文件，即使 .mmat 中仍然引用该哈希。
> **处理方案：通过 flatc JSON 编辑 .mmat，将哈希重定向到已知的 .texture**
> 文件，或删除该槽位让引擎使用默认山。

### 面部法线模型   `fn/fn1400/`

| 文件 | 大小 | 说明 |
|------|------|-------------|
| `fn1400.minfo` | 1.4 KB | 模型结构入口 — 描述 mesh 分段、材质槽、骨架绑定 |
| `fn1400.skeleton` | 1.5 KB | 骨架定义 — 骨骼名称、层级、绑定姿势 (T-Pose) |
| `vars/0.mmat` | 3.0 KB | 材质变体 0 -- 默认服装 |
| `vars/1.mmat` | 3.0 KB | 材质变体 1 -- 变体 1 |
| `vars/2.mmat` | 3.0 KB | 材质变体 2 -- 变体 2 |
| `vars/3.mmat` | 3.0 KB | 材质变体 3 -- 变体 3 |

#### 贴图引用（来自 vars/*.mmat）

| 哈希(前16位) | 完整哈希 | 贴图名 | 来源mmat |
|---|---|---|---|
| `59d696806a755f87` | `59d696806a755f873f6f62ed4186a16faabc0a7460591efeafc3aa895d67ffda` | `pre_flatnorm` | `0.mmat` |
| `3eec5e12fec1b565` | `3eec5e12fec1b5658b5b5b0f8bfea0c66de15c4c67d6a707379729c3808615ef` | `fn1400_r_eye_lod0_conj` | `0.mmat` |
| `bff6c19450f3ea92` | `bff6c19450f3ea92bfe820060baa7a0e958e6b38eb1fe6a0dd912a41e1d15c4d` | `fn1400_l_eye_lod0_conj` | `0.mmat` |

> **提取说明：这些哈希指向 Granite VT 图块（albd/nrml/msk1-2）。**
> GraniteTextureReader 版本须与游戏构建匹配——游戏更新后部分图块可能
> 返回 0 个文件，即使 .mmat 中仍然引用该哈希。
> **处理方案：通过 flatc JSON 编辑 .mmat，将哈希重定向到已知的 .texture**
> 文件，或删除该槽位让引擎使用默认山。

### 面部部件模型   `fp/fp1400/`

| 文件 | 大小 | 说明 |
|------|------|-------------|
| `fp1400.minfo` | 3.7 KB | 模型结构入口 — 描述 mesh 分段、材质槽、骨架绑定 |
| `fp1400.mmesh` | 1.8 MB | 本地网格缓存（同时存在于 model_streaming/） |
| `fp1400.skeleton` | 6.9 KB | 骨架定义 — 骨骼名称、层级、绑定姿势 (T-Pose) |
| `vars/0.mmat` | 4.2 KB | 材质变体 0 -- 默认服装 |
| `vars/1.mmat` | 4.2 KB | 材质变体 1 -- 变体 1 |
| `vars/10.mmat` | 4.2 KB | 材质变体 10 -- 变体 10 |
| `vars/2.mmat` | 4.2 KB | 材质变体 2 -- 变体 2 |
| `vars/3.mmat` | 4.2 KB | 材质变体 3 -- 变体 3 |
| `vars/4.mmat` | 4.2 KB | 材质变体 4 -- 变体 4 |
| `vars/5.mmat` | 4.2 KB | 材质变体 5 -- 变体 5 |
| `vars/6.mmat` | 4.2 KB | 材质变体 6 -- 变体 6 |
| `vars/7.mmat` | 4.2 KB | 材质变体 7 -- 变体 7 |
| `vars/8.mmat` | 4.2 KB | 材质变体 8 -- 变体 8 |
| `vars/9.mmat` | 4.2 KB | 材质变体 9 -- 变体 9 |

#### 贴图引用（来自 vars/*.mmat）

| 哈希(前16位) | 完整哈希 | 贴图名 | 来源mmat |
|---|---|---|---|
| `c7b1848cdd4b1f45` | `c7b1848cdd4b1f453808480ade9485775c9cfa9fdf7aceb8c7e96e79c6d8e923` | `pl1400_hair01_lod0_nrml` | `0.mmat` |
| `996aaa772426bf3d` | `996aaa772426bf3dd97987bad81f53d2b86181aa675f76e84831902f78c02b9c` | `pl1400_body03_lod0_nrml` | `0.mmat` |
| `2ae69ddb6a24ca92` | `2ae69ddb6a24ca92614f4b57b4aee9529645d7365b7f045a0a68a9f13503ae0b` | `pre_flatnorm` | `0.mmat` |
| `aada41987024e2e6` | `aada41987024e2e6f3f2b5a20e0e5798afce53d13aa59a73e77507c5eac9b29f` | `fp1400_r_eye_lod0_conj` | `0.mmat` |
| `c25189db93a5ce70` | `c25189db93a5ce70763c3bf18b42026f86f11fa1ce61c1ac6ab9483e7d638cb2` | `fp1400_l_eye_lod0_conj` | `0.mmat` |
| `b8ead3a6f6b64c14` | `b8ead3a6f6b64c14955af89a2d9f5cc7734dc660574a46b80c72e62741dfd6dc` | `pl1400_hair01_lod0_nrml` | `1.mmat` |
| `79d560a2500348f6` | `79d560a2500348f67d97950a6289e2bde58ccc45a6150a0d1792e68051707131` | `pl1400_body03_lod0_nrml` | `1.mmat` |
| `890f4762be5fa06d` | `890f4762be5fa06d1f9088e1c0e9575f7c249abc156b96d896d2bc3a8f9e60b5` | `pre_flatnorm` | `1.mmat` |
| `a3ba4c6890ea8799` | `a3ba4c6890ea879940915e9ab0b6fa67781273776a458a41f28ee354f456a916` | `fp1400_r_eye_lod0_conj` | `1.mmat` |
| `c8c47ded7ebeeb5b` | `c8c47ded7ebeeb5bd36d5cb5410984382416d29c5cfdd5d112ca8eb490d9e02b` | `fp1400_l_eye_lod0_conj` | `1.mmat` |
| `ce3ad3fbcfbb2cef` | `ce3ad3fbcfbb2cef9e82f5f0f9902e69c2871c6301229b72cb16db72cab63309` | `pl1400_body03_lod0_nrml` | `10.mmat` |
| `52c38316f60016cf` | `52c38316f60016cf79b4d747edefeb8328ddfa11d93694ebd57a8db4dd6cdf2a` | `pl1400_hair01_lod0_nrml` | `2.mmat` |
| `6c04f54d9841a058` | `6c04f54d9841a0587026a8ce4b0b31d41f7a914fe4169d0c093f5b4065bc7b38` | `pl1400_body03_lod0_nrml` | `2.mmat` |
| `0dfff38ad314afbb` | `0dfff38ad314afbb45f32947e20a5d2b980c177f30db870a42cae089defbfb1e` | `pre_flatnorm` | `2.mmat` |
| `250a63230f6d71b5` | `250a63230f6d71b551dbf7830b3a2efa830cce8393cdde80ace886c9f4f5ad82` | `fp1400_r_eye_lod0_conj` | `2.mmat` |
| `2836129a9c3f06a1` | `2836129a9c3f06a1c2605a751781897901c134f104201cd22fafc34dd3813bdb` | `fp1400_l_eye_lod0_conj` | `2.mmat` |
| `7d4d946c3af4aace` | `7d4d946c3af4aace5148cc7d9476d37a00b7defe592591561149b135ee6442f6` | `pl1400_hair01_lod0_nrml` | `3.mmat` |
| `0c9aa1b6a57e68e0` | `0c9aa1b6a57e68e06a721b04d633e4587523ac5ce85d1b3a9eefe9f45339a240` | `pl1400_body03_lod0_nrml` | `3.mmat` |
| `0f2f634dfec78c22` | `0f2f634dfec78c22ab8d855c68766dc2797fb90ed1d59a0677e9de7c6ed7c16d` | `pre_flatnorm` | `3.mmat` |
| `6d2195a7fc9140e6` | `6d2195a7fc9140e69e3b7fa8f2209a663723c7eee939a4d6fd494cc013f5ced5` | `fp1400_r_eye_lod0_conj` | `3.mmat` |
| `f6253d2ba80152a6` | `f6253d2ba80152a68f7427393247db172f770714e5a70113a27e96be7321e5d2` | `fp1400_l_eye_lod0_conj` | `3.mmat` |
| `fad7834b24371263` | `fad7834b243712632c4763350b5bb99049c0a10224fbb3c3026deb33af4af87a` | `pl1400_hair01_lod0_nrml` | `4.mmat` |
| `c27bb696c9b46525` | `c27bb696c9b46525ff026292b1576870ba1c488c8798ede5ccf6419844d14a41` | `pl1400_body03_lod0_nrml` | `4.mmat` |
| `681df04157bf32f3` | `681df04157bf32f3ea6915ec1902ad7ea8e829951b013b652980a947b84f8c43` | `pl1400_hair01_lod0_nrml` | `5.mmat` |
| `6395c0e90109b442` | `6395c0e90109b4427916e88d991d00c2e80920d67aee8262243064050670bf8e` | `pl1400_body03_lod0_nrml` | `5.mmat` |
| `025cbe12a4713861` | `025cbe12a4713861f118527915015ccc59d60fcf56355eef13dc439bb3e22aea` | `pl1400_hair01_lod0_nrml` | `6.mmat` |
| `15a405e08053ee29` | `15a405e08053ee295fcfce682eb56600a02e14549dbce0c0a220d8b340069dfb` | `pl1400_body03_lod0_nrml` | `6.mmat` |
| `b6e5a6a9301f5ae5` | `b6e5a6a9301f5ae55924d4337c94bedd60dbaf4653261f0c2ae967364806ed95` | `pre_flatnorm` | `6.mmat` |
| `38305c30800e891f` | `38305c30800e891f00ec1f0b551b5a02264bd6660e0ddfffc6925632192220e5` | `fp1400_r_eye_lod0_conj` | `6.mmat` |
| `44ac90189328eef9` | `44ac90189328eef9412d6b063fde2ccbcf501df667e566eb593eb1edf6739170` | `fp1400_l_eye_lod0_conj` | `6.mmat` |
| `680e4632c872679a` | `680e4632c872679aa7a2c780077fe09523f646f371f9ef6a4e460621076e7130` | `pl1400_hair01_lod0_nrml` | `7.mmat` |
| `3e06d66a2405cba6` | `3e06d66a2405cba6246be020c32f003e3390a44a2eecf2850eaeaa3fcf566542` | `pl1400_body03_lod0_nrml` | `7.mmat` |
| `33572f204080a9d4` | `33572f204080a9d40ffc7ed4f155f403f7bf2c8ff3ffba2ef667199c22f59f10` | `pl1400_body03_lod0_nrml` | `8.mmat` |
| `457356a74e3e860a` | `457356a74e3e860a2d51463ecfa7c25fdc43f337b5a59eabd44e2175de9d222b` | `pl1400_hair01_lod0_nrml` | `9.mmat` |
| `06296cdaf9346159` | `06296cdaf9346159c353f2a4c53bb1eea4afa16175ee2251758b2262a8289ab6` | `pl1400_body03_lod0_nrml` | `9.mmat` |
| `81d70e186992e5c9` | `81d70e186992e5c9f532f1c1d5718e849399f43dd2fd6dab346dbf172b4335ff` | `pre_flatnorm` | `9.mmat` |
| `8e37e4e120b38511` | `8e37e4e120b38511a7caaac9cd0f8d2c9ae8c431678b4a08b01b92780e3ba687` | `fp1400_r_eye_lod0_conj` | `9.mmat` |
| `75657dfd5282f423` | `75657dfd5282f423c4ac1c3e9d656c5995e6963bdfaa773113519b9347326c2e` | `fp1400_l_eye_lod0_conj` | `9.mmat` |

> **提取说明：这些哈希指向 Granite VT 图块（albd/nrml/msk1-2）。**
> GraniteTextureReader 版本须与游戏构建匹配——游戏更新后部分图块可能
> 返回 0 个文件，即使 .mmat 中仍然引用该哈希。
> **处理方案：通过 flatc JSON 编辑 .mmat，将哈希重定向到已知的 .texture**
> 文件，或删除该槽位让引擎使用默认山。

### 近景/NPC 辅助网格   `np/np1400/`

| 文件 | 大小 | 说明 |
|------|------|-------------|
| `np1400.minfo` | 2.8 KB | 模型结构入口 — 描述 mesh 分段、材质槽、骨架绑定 |
| `np1400.skeleton` | 4.9 KB | 骨架定义 — 骨骼名称、层级、绑定姿势 (T-Pose) |
| `np1400.sop` | 1.7 KB | 形态/遮挡/物理附加数据 (Shape/Occlusion/Physics) |
| `vars/0.mmat` | 3.2 KB | 材质变体 0 -- 默认服装 |
| `vars/1.mmat` | 3.3 KB | 材质变体 1 -- 变体 1 |
| `vars/2.mmat` | 3.3 KB | 材质变体 2 -- 变体 2 |
| `vars/3.mmat` | 3.3 KB | 材质变体 3 -- 变体 3 |

#### 贴图引用（来自 vars/*.mmat）

| 哈希(前16位) | 完整哈希 | 贴图名 | 来源mmat |
|---|---|---|---|
| `0e67a2cd330652c5` | `0e67a2cd330652c51ed6112313660d5e66661a500ce4df01596a1e8e1799c320` | `np1400_skin_lod0_nrml` | `0.mmat` |
| `74c891f9004cbff1` | `74c891f9004cbff1354cab65e1df3a2caa79c90eeffdadc5247b01ecf4c1371d` | `np1400_cloth_lod0_nrml` | `0.mmat` |
| `d057bc3a321ca1cb` | `d057bc3a321ca1cb91f4b78fcb90220040fb7460b076f8341a03e30208eb15b8` | `np1400_a00_hair_lod0_nrml` | `0.mmat` |

> **提取说明：这些哈希指向 Granite VT 图块（albd/nrml/msk1-2）。**
> GraniteTextureReader 版本须与游戏构建匹配——游戏更新后部分图块可能
> 返回 0 个文件，即使 .mmat 中仍然引用该哈希。
> **处理方案：通过 flatc JSON 编辑 .mmat，将哈希重定向到已知的 .texture**
> 文件，或删除该槽位让引擎使用默认山。

### 武器模型   `wp/wp1400/`

| 文件 | 大小 | 说明 |
|------|------|-------------|
| `wp1400.minfo` | 1.0 KB | 模型结构入口 — 描述 mesh 分段、材质槽、骨架绑定 |
| `wp1400.skeleton` | 1.1 KB | 骨架定义 — 骨骼名称、层级、绑定姿势 (T-Pose) |
| `vars/0.mmat` | 844 B | 材质变体 0 -- 默认服装 |

#### 贴图引用（来自 vars/*.mmat）

| 哈希(前16位) | 完整哈希 | 贴图名 | 来源mmat |
|---|---|---|---|
| `9d505f9c177d9c8b` | `9d505f9c177d9c8b245849e257cd00b1753a62840272d267b21eba4c150793c1` | `wp1400_weapon_lod0_nrml` | `0.mmat` |

> **提取说明：这些哈希指向 Granite VT 图块（albd/nrml/msk1-2）。**
> GraniteTextureReader 版本须与游戏构建匹配——游戏更新后部分图块可能
> 返回 0 个文件，即使 .mmat 中仍然引用该哈希。
> **处理方案：通过 flatc JSON 编辑 .mmat，将哈希重定向到已知的 .texture**
> 文件，或删除该槽位让引擎使用默认山。

---

## 网格流式层

model_streaming/lod{0-3}/

| LOD | 文件 | 大小 | 说明 |
|-----|------|------|-------------|
| lod0 | `pl1400.mmesh` | 3.2 MB | 角色主体网格 |
| lod0 | `fn1400.mmesh` | 409.4 KB | 面部法线网格 |
| lod0 | `fp1400.mmesh` | 1.8 MB | 面部部件网格（眼/嘴动态） |
| lod0 | `np1400.mmesh` | 726.2 KB | 近景/NPC 辅助网格 |
| lod0 | `wp1400.mmesh` | 215.6 KB | 武器网格 |
| lod1 | `pl1400.mmesh` | 4.1 MB | 角色主体网格 |
| lod1 | `fn1400.mmesh` | 90.3 KB | 面部法线网格 |
| lod1 | `fp1400.mmesh` | 309.4 KB | 面部部件网格（眼/嘴动态） |
| lod1 | `np1400.mmesh` | 438.6 KB | 近景/NPC 辅助网格 |
| lod1 | `wp1400.mmesh` | 141.7 KB | 武器网格 |
| lod2 | `pl1400.mmesh` | 2.0 MB | 角色主体网格 |
| lod2 | `fn1400.mmesh` | 52.5 KB | 面部法线网格 |
| lod2 | `fp1400.mmesh` | 173.6 KB | 面部部件网格（眼/嘴动态） |
| lod2 | `np1400.mmesh` | 262.4 KB | 近景/NPC 辅助网格 |
| lod2 | `wp1400.mmesh` | 83.5 KB | 武器网格 |
| lod3 | `pl1400.mmesh` | 512.2 KB | 角色主体网格 |
| lod3 | `fn1400.mmesh` | 28.8 KB | 面部法线网格 |
| lod3 | `fp1400.mmesh` | 56.9 KB | 面部部件网格（眼/嘴动态） |
| lod3 | `np1400.mmesh` | 99.4 KB | 近景/NPC 辅助网格 |
| lod3 | `wp1400.mmesh` | 50.1 KB | 武器网格 |

---

## 行为层

path: `data/pl/pl1400/`

| 类型 | 数量 | 总大小 | 说明 |
|------|-------|----------|-------------|
| `.mot` | 518 | 18.6 MB | 骨骼动画关键帧数据 |
| `.bxm` | 2338 | 1.2 MB | BXM 二进制 XML（转换: GBFRDataTools b-convert -i <file>.bxm） |
| `pl1400.lst` | 1 | 35.0 KB | 资源清单列表 |
| `pl1400_ability.aib` | 1 | 788 B | AI 行为数据 (AIB) |
| `pl1400_charainfo.cib` | 1 | 70 B | 碰撞/AI 行为包 (CIB) |

**BXM 类型分布:**

| 后缀模式 | 数量 | 含义 |
|---|---|---|
| `*_seq_edit_facialmotion.bxm` | 645 | 表情/面部动作控制 |
| `*_seq_edit_flags.bxm` | 538 | 状态标志开关 |
| `*_seq_edit_effect.bxm` | 476 | 特效触发时间轴 |
| `*_seq_edit_ik.bxm` | 378 | IK 骨骼调整 |
| `*_seq_edit_attack.bxm` | 177 | 其他 |
| `*_seq_edit_camera.bxm` | 64 | 摄影机参数 |
| `*_seq_edit_cloth.bxm` | 43 | 布料物理参数覆盖 |
| `*_seq_edit_eyecontrol.bxm` | 11 | 其他 |
| `*_seq_edit_se.bxm` | 5 | 其他 |
| `*_seq_edit_vib.bxm` | 1 | 其他 |

**动画 ID 范围分布：**

| ID范围 | 数量 | 总KB | 分类推断 |
|---|---|---|---|
| `0x00xx` (0000~00a2) | 85 | 1795 | 基础动作 — 待机/移动/受击/闪避 |
| `0x05xx` (0500~05a0) | 29 | 798 | 技能动作组 A |
| `0x06xx` (0620~06a2) | 19 | 481 | 技能动作组 B |
| `0x0axx` (0a20~0a33) | 6 | 101 | 技能附加动作 |
| `0x0bxx` (0b00~0bb0) | 35 | 690 | 技能动作组 C |
| `0x18xx` (1800~1820) | 4 | 839 | 奥义动作 (SBA / Skybound Art) |
| `0x30xx` (3000~304a) | 33 | 1613 | 扩展技能组 A |
| `0x31xx` (3100~31f3) | 4 | 154 | 扩展技能组 B |
| `0x32xx` (3200~32b2) | 12 | 580 | 扩展技能组 C / 连招 |
| `0x33xx` (3300~3312) | 6 | 373 | 扩展技能组 D |
| `0x34xx` (3410~348c) | 31 | 1226 | 扩展技能组 E |
| `0x35xx` (3510~358c) | 31 | 2132 | 扩展技能组 F |
| `0x3axx` (3a00~3a10) | 2 | 142 | 特殊/互动动作 |
| `0x3fxx` (3f00~3f3f) | 48 | 929 | 菜单/胜利/待机变体 |
| `0xa5xx` (a500~a50c) | 6 | 69 | 服装变体附加动作 |
| `0xb0xx` (b000~b0a2) | 64 | 1178 | 服装变体基础动作镜像 |
| `0xb5xx` (b500~b5a0) | 29 | 850 | 服装变体技能镜像 A |
| `0xb6xx` (b620~b6a2) | 19 | 493 | 服装变体技能镜像 B |
| `0xbaxx` (ba20~ba23) | 3 | 49 | 服装变体附加动作 B |
| `0xbbxx` (bb10~bb3a) | 14 | 225 | 服装变体补充动作 |
| `0xe3xx` (e320~e360) | 2 | 49 | 过场前置动作 A |
| `0xe4xx` (e420~e460) | 2 | 37 | 过场前置动作 B |
| `0xeaxx` (ea00~eac0) | 6 | 250 | 过场动画 A |
| `0xebxx` (eb20~ebc0) | 9 | 1764 | 过场动画 B（大型） |
| `0xecxx` (ec40~ecc0) | 7 | 341 | 过场动画 C |
| `0xedxx` (ed00~ed90) | 11 | 1857 | 活动演示动画 (Event Demo) |
| `0xfaxx` (fa00~fa00) | 1 | 7 | 预留/占位动画 |

> 注：ID 为十六进制，文件内无语义名称，分类依 GBFR 社区约定推断。
> 0x18xx 范围通常为 Skybound Art 奥义动画（帧数最多，文件最大）。
> 0xedxx 范围为 Event Demo 过场动画，部分可能跨角色共用骨架。

**最大动画文件 (Top 10)：**

| 文件 | 大小 | 分类 |
|---|---|---|
| `pl1400_1820.mot` | 642.2 KB | 奥义动作 (SBA / Skybound Art) |
| `pl1400_ed10.mot` | 370.1 KB | 活动演示动画 (Event Demo) |
| `pl1400_ed00.mot` | 346.9 KB | 活动演示动画 (Event Demo) |
| `pl1400_358a.mot` | 345.7 KB | 扩展技能组 F |
| `pl1400_ebb3.mot` | 329.0 KB | 过场动画 B（大型） |
| `pl1400_ebb1.mot` | 327.5 KB | 过场动画 B（大型） |
| `pl1400_ebb2.mot` | 327.5 KB | 过场动画 B（大型） |
| `pl1400_ebb0.mot` | 327.5 KB | 过场动画 B（大型） |
| `pl1400_3312.mot` | 256.8 KB | 扩展技能组 D |
| `pl1400_358c.mot` | 248.7 KB | 扩展技能组 F |

---

## 布料物理层

path: `data/pl/pl1400/cloth/`

| 文件 | 大小 | 说明 |
|------|------|------|
| `pl1400_0_0_clh.bxm` | 1.4 KB | 布料层级 — 节点链顺序（骨骼链接） (组 0) |
| `pl1400_0_0_clp.bxm` | 11.7 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 0) |
| `pl1400_0_1_clh.bxm` | 601 B | 布料层级 — 节点链顺序（骨骼链接） (组 1) |
| `pl1400_0_1_clp.bxm` | 4.1 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 1) |
| `pl1400_0_2_clh.bxm` | 2.7 KB | 布料层级 — 节点链顺序（骨骼链接） (组 2) |
| `pl1400_0_2_clp.bxm` | 9.2 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 2) |
| `pl1400_0_3_clh.bxm` | 1.1 KB | 布料层级 — 节点链顺序（骨骼链接） (组 3) |
| `pl1400_0_3_clp.bxm` | 2.1 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 3) |
| `pl1400_0_4_clh.bxm` | 2.7 KB | 布料层级 — 节点链顺序（骨骼链接） (组 4) |
| `pl1400_0_4_clp.bxm` | 3.2 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 4) |
| `pl1400_0_5_clh.bxm` | 2.5 KB | 布料层级 — 节点链顺序（骨骼链接） (组 5) |
| `pl1400_0_5_clp.bxm` | 6.1 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 5) |
| `pl1400_0_6_clh.bxm` | 2.7 KB | 布料层级 — 节点链顺序（骨骼链接） (组 6) |
| `pl1400_0_6_clp.bxm` | 4.7 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 6) |
| `pl1400_0_7_clh.bxm` | 4.6 KB | 布料层级 — 节点链顺序（骨骼链接） (组 7) |
| `pl1400_0_7_clp.bxm` | 2.3 KB | 布料参数 — 重力/弹性/碰撞/风阻 (组 7) |
| `pl1400_rmslst.bxm` | 3.2 KB | 参考网格弹簧列表 — 弹簧约束 |

> **布料组数**: `_clp` x 8 ,  `_clh` x 8
> 转换: GBFRDataTools.exe b-convert -i <file>.bxm

---

## 贴图文件层

data/texture/{2k,4k}/ — WTB 格式，mod 可直接替换


### 分辨率: 2k

| 文件 | 大小 | 槽位 | 说明 |
|------|------|------|------|
| `fn1400_face_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `fn1400_face_lod0_msk4.texture` | 89.5 KB | `msk4` | 附加遮罩通道 |
| `fn1400_face_lod0_msk5.texture` | 89.5 KB | `msk5` | 附加遮罩通道 |
| `fp1400_face_lod0_msk3.texture` | 1.3 MB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `fp1400_face_lod0_msk4.texture` | 89.5 KB | `msk4` | 附加遮罩通道 |
| `fp1400_face_lod0_msk5.texture` | 89.5 KB | `msk5` | 附加遮罩通道 |
| `np1400_a00_hair_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `np1400_cloth2_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `np1400_skin_lod0_msk2.texture` | 89.5 KB | `msk2` | 粗糙度/高光遮罩 |
| `pl1400_body01_1_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_body02_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_body03_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_hair01_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_hair02_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_sheath_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_sheath04_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_sheath05_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_skin_lod0_msk2.texture` | 89.5 KB | `msk2` | 粗糙度/高光遮罩 |
| `shuf2_fn1400_face_lod0_msk4.texture` | 89.5 KB | `msk4` | 附加遮罩通道 |
| `wp1400_weapon_lod0_msk3.texture` | 89.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |

### 分辨率: 4k

| 文件 | 大小 | 槽位 | 说明 |
|------|------|------|------|
| `fn1400_face_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `fn1400_face_lod0_msk4.texture` | 345.5 KB | `msk4` | 附加遮罩通道 |
| `fn1400_face_lod0_msk5.texture` | 345.5 KB | `msk5` | 附加遮罩通道 |
| `fp1400_face_lod0_msk3.texture` | 1.3 MB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `fp1400_face_lod0_msk4.texture` | 345.5 KB | `msk4` | 附加遮罩通道 |
| `fp1400_face_lod0_msk5.texture` | 345.5 KB | `msk5` | 附加遮罩通道 |
| `np1400_a00_hair_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `np1400_cloth2_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `np1400_skin_lod0_msk2.texture` | 345.5 KB | `msk2` | 粗糙度/高光遮罩 |
| `pl1400_body01_1_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_body02_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_body03_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_hair01_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_hair02_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_sheath_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_sheath04_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_sheath05_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |
| `pl1400_skin_lod0_msk2.texture` | 345.5 KB | `msk2` | 粗糙度/高光遮罩 |
| `shuf2_fn1400_face_lod0_msk4.texture` | 345.5 KB | `msk4` | 附加遮罩通道 |
| `wp1400_weapon_lod0_msk3.texture` | 345.5 KB | `msk3` | 发光/自定义遮罩（主要可替换层） |

> **唯一贴图文件总数**: 20  (x2 = 40)
> 替换 2k 版本用于标准 mod；同时替换 4k 以支持高清。

#### .texture 文件的 mod 制作方案

| 方案 | 适用场景 | 操作步骤 |
|---|---|---|
| **直接替换** | .texture 存在且可提取 | 提取 DDS (nier_cli/WtbExtract) → 编辑 → WtbPack 重打包 |
| **重定向 (via mmat)** | Granite 哈希在游戏更新后无法提取 | flatc 解码 .mmat → 修改哈希字段指向新 .texture → 重新编码 |
| **忽略槽位** | mod 不需要该贴图 | flatc 解码 .mmat → 删除/置空该槽位 → 重新编码 |

---

## 资源总览

| 字段 | 値 |
|---|---|
| **源资源副本** | `E:\BaiduSyncdisk\在做工程\渲染研究\村希罗\gbfrMod\GBFR_modtools\explore\explore_output\pl1400_sources` |
| **复制文件数** | 2981 |
| **复制总大小** | 50.5 MB |

---
*由 explore_char.ps1 自动生成 | GBFR_modtools*