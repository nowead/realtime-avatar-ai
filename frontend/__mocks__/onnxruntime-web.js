// frontend/__mocks__/onnxruntime-web.js

// Jest의 모킹 함수들을 사용합니다.
const mockRun = jest.fn().mockResolvedValue({
  // 'output' 키는 sileroVadRunner.js 코드에서 사용됩니다.
  // 실제 모델의 출력 형식에 맞춰 조정이 필요할 수 있습니다.
  output: { data: [0.5] }, // 예시: 음성이 감지되지 않았다고 가정
  // sileroVadRunner.js에서 'stateN' 또는 'state' 같은 키를 찾으므로,
  // 그에 맞는 state를 반환하도록 합니다.
  stateN: { dims: [2, 1, 128], data: new Float32Array(2 * 1 * 128).fill(0) }
});

const mockCreate = jest.fn().mockResolvedValue({
  run: mockRun,
});

class InferenceSession {
  static create = mockCreate; // 정적 create 메소드
  constructor() {
    // 생성자 모킹 (필요에 따라)
    this.run = mockRun;
  }
}

class Tensor {
  constructor(type, data, dims) {
    this.type = type;
    this.data = data;
    this.dims = dims;
    // 실제 Tensor 클래스와 유사하게 필요한 속성들을 추가할 수 있습니다.
  }
}

module.exports = {
  InferenceSession,
  Tensor,
  // 라이브러리에서 사용되는 다른 상수나 함수가 있다면 여기에 추가합니다.
};