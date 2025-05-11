// frontend/__mocks__/onnxruntime-web.js
module.exports = {
  InferenceSession: {
    create: jest.fn().mockResolvedValue({
      run: jest.fn().mockResolvedValue({
        output: { data: [0.8] } // 예시 출력, 필요에 따라 수정
      })
    })
  },
  Tensor: jest.fn()
};