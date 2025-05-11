// frontend/__mocks__/babylonjs.js

const mockMorphTargetManager = {
  numTargets: 0,
  getTarget: jest.fn().mockReturnValue({ influence: 0 }),
  getTargetIndexByName: jest.fn().mockReturnValue(-1),
  // 필요에 따라 더 많은 메소드/속성 추가
};

const mockMesh = {
  position: { x: 0, y: 0, z: 0 },
  rotation: { x: 0, y: 0, z: 0 },
  morphTargetManager: mockMorphTargetManager,
  // 필요에 따라 더 많은 메소드/속성 추가
};

const mockScene = {
  clearColor: {},
  debugLayer: { show: jest.fn() },
  render: jest.fn(),
  // 필요에 따라 더 많은 메소드/속성 추가
};

const mockEngine = {
  runRenderLoop: jest.fn(callback => callback()),
  resize: jest.fn(),
  // 필요에 따라 더 많은 메소드/속성 추가
};

const mockArcRotateCamera = jest.fn().mockImplementation(() => ({
  attachControl: jest.fn(),
  setTarget: jest.fn(),
  inputs: {
    attached: {
      pointers: {
        angularSensibilityX: 0,
        buttons: []
      }
    }
  },
  lowerRadiusLimit: 0,
  upperRadiusLimit: 0,
  lowerAlphaLimit: 0,
  upperAlphaLimit: 0,
  lowerBetaLimit: 0,
  upperBetaLimit: 0,
  wheelDeltaPercentage: 0,
  // 필요에 따라 더 많은 메소드/속성 추가
}));


module.exports = {
  Engine: jest.fn().mockImplementation(() => mockEngine),
  Scene: jest.fn().mockImplementation(() => mockScene),
  Color4: jest.fn().mockImplementation((r, g, b, a) => ({ r, g, b, a })),
  Vector3: jest.fn().mockImplementation((x, y, z) => ({ x, y, z })),
  ArcRotateCamera: mockArcRotateCamera,
  HemisphericLight: jest.fn(),
  SceneLoader: {
    ImportMeshAsync: jest.fn().mockResolvedValue({
      meshes: [mockMesh], // 최소한 하나의 메쉬를 반환하도록 설정
      // 다른 필요한 속성들 (skeletons, animationGroups 등)
    }),
  },
  Scalar: {
    Clamp: jest.fn((value, min, max) => Math.min(Math.max(value, min), max)),
  },
  // avatar.js 또는 다른 파일에서 BABYLON.* 으로 접근하는 모든 객체/함수를 여기에 추가
  // 예: BABYLON.Matrix, BABYLON.Quaternion 등
};