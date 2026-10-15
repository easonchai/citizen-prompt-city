// 3D view of the physical badge (ESP32-2424S012C in its stock case). Look only: no touch reaches the core.
// Units are mm. The puck axis is Z, the front (screen) faces +Z, the top of the screen is +Y.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

export function create(canvas, screenCanvas) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.setSize(720, 720, false); // CSS sizes the canvas; the buffer stays square
  const scene = new THREE.Scene();
  scene.background = new THREE.Color('#0d1117'); // page --bg
  const camera = new THREE.PerspectiveCamera(35, 1, 1, 1000);
  camera.position.set(30, 22, 92);

  // three-point rig plus a little ambient so the matte black reads
  scene.add(new THREE.AmbientLight(0xffffff, 0.35));
  const light = (color, i, x, y, z) => { const l = new THREE.DirectionalLight(color, i); l.position.set(x, y, z); scene.add(l); };
  light(0xfff4e6, 2.4, 60, 80, 100); // key
  light(0xb8c8ff, 0.9, -90, 10, 50); // fill
  light(0xffffff, 1.6, -20, 60, -110); // rim

  const badge = new THREE.Group();
  scene.add(badge);
  const caseMat = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.85 });
  const holeMat = new THREE.MeshStandardMaterial({ color: 0x020202, roughness: 1 });
  const partMat = new THREE.MeshStandardMaterial({ color: 0xb8a27a, roughness: 0.6 });

  // Case: 44 mm puck, 13 mm thick, rounded back edge, domed front edge, recessed window opening of 32.4 mm.
  const profile = [[0, 0], [20.6, 0], [21.6, 0.4], [22, 1.4], [22, 9], [21.8, 10.4], [21.2, 11.6], [20.2, 12.5], [18.8, 12.95],
    [17.2, 13], [16.5, 12.85], [16.2, 12.5], [16.2, 12.2], [0, 12.2]].map(([r, z]) => new THREE.Vector2(r, z));
  const shell = new THREE.LatheGeometry(profile, 96).rotateX(Math.PI / 2); // lathe Y -> Z
  badge.add(new THREE.Mesh(shell, caseMat));

  // Window: thin glossy dark glass bezel ring around the 240x240 framebuffer.
  const ring = new THREE.Mesh(new THREE.RingGeometry(15.4, 16.2, 96),
    new THREE.MeshStandardMaterial({ color: 0x05070a, roughness: 0.08, metalness: 0.4 }));
  ring.position.z = 12.45;
  badge.add(ring);
  const tex = new THREE.CanvasTexture(screenCanvas);
  tex.magFilter = tex.minFilter = THREE.NearestFilter; tex.generateMipmaps = false;
  tex.colorSpace = THREE.SRGBColorSpace;
  const screenMat = new THREE.MeshBasicMaterial({ map: tex, toneMapped: false });
  const screen = new THREE.Mesh(new THREE.CircleGeometry(15.4, 96), screenMat); // circle UVs inscribe the square texture
  screen.position.z = 12.4;
  badge.add(screen);

  // Back (faces -Z): BOOT and RESET holes near the top, connector cutout lower right as seen from behind (world -X).
  const onBack = (geo, mat, x, y) => { const m = new THREE.Mesh(geo, mat); m.position.set(x, y, -0.1); badge.add(m); return m; };
  const disc = r => new THREE.CylinderGeometry(r, r, 0.3, 32).rotateX(Math.PI / 2);
  for (const x of [-10, 10]) { onBack(disc(1.5), holeMat, x, 12); onBack(disc(0.9), partMat, x, 12).position.z = -0.15; }
  onBack(new THREE.BoxGeometry(6, 4, 0.3), holeMat, -7, -8);
  onBack(new THREE.BoxGeometry(4.4, 2.2, 0.3), partMat, -7, -8).position.z = -0.15;
  // USB-C slot on the side edge at the bottom.
  const usb = new THREE.Mesh(new THREE.BoxGeometry(9, 1.2, 3.2), holeMat);
  usb.position.set(0, -21.75, 5.5);
  badge.add(usb);

  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true; controls.enablePan = false;
  controls.minDistance = 50; controls.maxDistance = 220;
  controls.autoRotate = true; controls.autoRotateSpeed = 1;
  controls.addEventListener('start', () => { controls.autoRotate = false; });
  controls.addEventListener('end', () => { controls.autoRotate = true; });
  canvas.addEventListener('dblclick', () => controls.reset());

  let active = false;
  renderer.setAnimationLoop(() => { if (active) { controls.update(); renderer.render(scene, camera); } });

  return {
    setActive(on) { active = on; },
    // Called whenever the sim redraws. level: backlight 0..100, 0 when the panel is asleep.
    update(level) {
      tex.needsUpdate = true;
      screenMat.color.setScalar(level ? 0.35 + 0.65 * level / 100 : 0); // same floor as the 2D CSS filter
    },
  };
}
