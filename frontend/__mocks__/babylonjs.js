// frontend/__mocks__/babylonjs.js
const mockMorphTarget = { influence: 0, name: 'mockTarget' };
const mockMorphTargetManager = {
  numTargets: 1,
  getTarget: jest.fn().mockReturnValue(mockMorphTarget),
};

module.exports = {
  Engine: jest.fn().mockImplementation(() => ({
    runRenderLoop: jest.fn(),
    resize: jest.fn(),
  })),
  Scene: jest.fn().mockImplementation(() => ({
    render: jest.fn(),
    // 필요한 Scene의 다른 메서드/프로퍼티 mock
  })),
  ArcRotateCamera: jest.fn().mockImplementation(() => ({
    attachControl: jest.fn(),
    wheelDeltaPercentage: 0.01,
  })),
  HemisphericLight: jest.fn(),
  Vector3: jest.fn(),
  SceneLoader: {
    ImportMesh: jest.fn((_meshesNames, _rootUrl, _sceneFilename, scene, successCallback) => {
      // 아바타 로드 시뮬레이션
      const meshes = [{ // 실제 메쉬 구조에 맞게 mock 필요
        name: "avatarMesh",
        position: { y: 0 },
        morphTargetManager: mockMorphTargetManager,
        // 필요한 다른 메쉬 프로퍼티
      }];
      if (successCallback) {
        successCallback(meshes, null, null, null);
      }
      return {
        // ImportMesh의 반환값 mock (필요한 경우)
      };
    }),
  },
  Scalar: {
    Clamp: jest.fn((value, min, max) => Math.min(Math.max(value, min), max)),
  },
  // 필요한 Babylon.js의 다른 객체/함수 mock
  MorphTargetManager: jest.fn().mockImplementation(() => mockMorphTargetManager),
};