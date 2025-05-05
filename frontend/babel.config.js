module.exports = {
    presets: [
      [
        '@babel/preset-env',
        {
          targets: '> 0.25%, not dead', // 브라우저 지원 범위 설정 (프로젝트에 맞게 조정)
        },
      ],
    ],
  };