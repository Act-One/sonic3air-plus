const fs = require('fs');
const path = require('path');

function loadGlslangFactory() {
  const candidates = [
    '@webgpu/glslang',
    path.resolve(__dirname, '../../../../.codex-temp/vulkan-shader-tools/node_modules/@webgpu/glslang'),
  ];

  for (const candidate of candidates) {
    try {
      return require(candidate);
    } catch (error) {
    }
  }

  throw new Error('Could not load @webgpu/glslang. Install it with "npm install @webgpu/glslang" or use the local Codex temp tool cache.');
}

function compileStage(glslang, sourcePath, stage, outputPath) {
  const source = fs.readFileSync(sourcePath, 'utf8');
  const spirv = glslang.compileGLSL(source, stage);
  fs.writeFileSync(outputPath, Buffer.from(spirv.buffer, spirv.byteOffset, spirv.byteLength));
  console.log(`Compiled ${path.basename(sourcePath)} -> ${path.basename(outputPath)}`);
}

async function main() {
  const glslangFactory = loadGlslangFactory();
  const glslang = await new Promise((resolve, reject) => {
    const module = glslangFactory();
    module.then(resolve);
    module.catch?.(reject);
    setTimeout(() => reject(new Error('Timed out waiting for glslang initialization')), 15000);
  });

  const shaderDir = path.resolve(__dirname, '../../data/shader/vulkan');
  const compileList = [
    ['copy.vert.glsl', 'vertex', 'copy.vert.spv'],
    ['upscaler_soft.frag.glsl', 'fragment', 'upscaler_soft.frag.spv'],
    ['upscaler_xbrz_pass0.frag.glsl', 'fragment', 'upscaler_xbrz_pass0.frag.spv'],
    ['upscaler_xbrz_pass1.frag.glsl', 'fragment', 'upscaler_xbrz_pass1.frag.spv'],
    ['upscaler_hq2x.frag.glsl', 'fragment', 'upscaler_hq2x.frag.spv'],
    ['upscaler_hq3x.frag.glsl', 'fragment', 'upscaler_hq3x.frag.spv'],
    ['upscaler_hq4x.frag.glsl', 'fragment', 'upscaler_hq4x.frag.spv'],
  ];

  for (const [inputName, stage, outputName] of compileList) {
    compileStage(glslang, path.join(shaderDir, inputName), stage, path.join(shaderDir, outputName));
  }
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : String(error));
  process.exit(1);
});
