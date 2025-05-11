// frontend/jest.config.js
module.exports = {
  testEnvironment: 'jest-environment-jsdom',
  // 필요한 경우, CSS, 이미지 파일 등을 Jest가 무시하도록 설정할 수 있습니다.
  moduleNameMapper: {
    '\\.(css|less|scss|sass)$': 'identity-obj-proxy', // CSS 모듈 mock
    '\\.(jpg|jpeg|png|gif|eot|otf|webp|svg|ttf|woff|woff2|mp4|webm|wav|mp3|m4a|aac|oga)$': '<rootDir>/__mocks__/fileMock.js', // 파일 mock
    '^onnxruntime-web$': '<rootDir>/__mocks__/onnxruntime-web.js', // onnxruntime-web mock
    '^babylonjs$': '<rootDir>/__mocks__/babylonjs.js' // Babylon.js mock
  },
  // 전역 API mock 설정 (필요시)
  // setupFilesAfterEnv: ['<rootDir>/jest.setup.js'],
  transform: {
    '^.+\\.jsx?$': 'babel-jest', // .js, .jsx 파일은 babel-jest로 트랜스폼
  },
  // 'src' 디렉토리 내의 __tests__ 폴더 또는 .test.js/.spec.js 파일을 테스트 대상으로 함
  testMatch: [
    '<rootDir>/src/**/__tests__/**/*.js?(x)',
    '<rootDir>/src/**/?(*.)+(spec|test).js?(x)',
  ],
  // 테스트 커버리지 리포트 설정 (선택 사항)
  // collectCoverage: true,
  // coverageDirectory: "coverage",
  // coverageReporters: ["json", "lcov", "text", "clover"],
};