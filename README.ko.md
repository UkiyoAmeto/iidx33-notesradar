# IIDX 33 NOTES RADAR Cap Unlock

> ## ⚠️ 중요 경고 (온라인 서버)
>
> **온라인 서버에 접속한 상태에서는 절대 `EnablePlayerCap` 을 끄지 마십시오.**
> 이 옵션을 끄면 플레이어 레이더(PNR)가 200.00 을 초과하여 서버에 비정상 값으로 기록될 수 있고,
> **계정 정지(BAN)로 이어질 수 있습니다**.
> 온라인 플레이 시에는 반드시 `EnablePlayerCap=1` 을 유지하십시오.
> 이 값을 바꾸는 것은 완전히 오프라인인 환경에서만 고려하십시오.

[简体中文](README.md) | [English](README.en.md) | [日本語](README.ja.md) | **한국어**

beatmania IIDX 33 Sparkle Shower (LDJ) 의 NOTES RADAR 상한 해제 플러그인입니다.

게임은 채보 레이더의 여섯 축을 **200.00** 으로 강제 클램프합니다. 이 플러그인은 그 상한을
제거하여 곡 선택 화면과 서브 화면에 실제 값(200 초과 포함)을 표시합니다. 반면
**플레이어 레이더(PNR)는 집계·보고 시점에 여전히 200.00 으로 캡**됩니다.

---

## 기능

| 기능 | 설명 | 옵션 |
|---|---|---|
| 채보 레이더 상한 해제 | 곡 선택 / 서브 화면에 실제 값 표시 (>200.00) | `EnableCap` |
| 플레이어 레이더 캡 | PNR 집계·보고는 200.00 유지 | `EnablePlayerCap` |
| 수동 재계산 | `Ctrl+Alt+R` 로 게임 자체의 전체 채보 레이더 재계산 실행 | `EnableRecalcKey` |

## 원리

각 축의 레이더 값은 원시 지표에 **4구간 조각별 선형 정규화 함수**를 적용해 얻습니다.
출력은 `[0, 2.0]` 이며 `10000` 을 곱해 1/100 고정소수점 정수로 저장됩니다.
200.00 이라는 상한은 `min(v, 200)` 명령이 아니라 **마지막 구간의 종점**
(`1.75 + 0.25 × 1.0 = 2.0`)입니다. 마지막 구간의 "비율을 1.0 으로 클램프" 분기를
NOP 로 바꾸면 값이 선형으로 외삽되어 단조성을 유지한 채 200.00 을 넘습니다.

전체 리버스 엔지니어링 정리(파라미터 표, 데이터 구조, 검증 방법)는
**[docs/algorithm.md](docs/algorithm.md)** 를 참고하십시오.

## 호환성

- **IIDX 33 Sparkle Shower**(bm2dx.dll 18,467,992 바이트) 와
  **IIDX 34 ZINRAI**(bm2dx.dll 18,200,216 바이트) 를 지원합니다. 실행 시 모듈의
  `SizeOfImage` 로 자동 판별합니다.
- DLL 은 각 패치 지점의 바이트 서명을 검증하며, 불일치 시 해당 변경을 건너뛰고
  로그에 기록합니다(게임을 손상시키지 않음).
- spice2x 플러그인과 공존하며, `-k` 인자로 로드됩니다.

## 빌드

C 컴파일러(MSVC / clang / zig)면 무엇이든 됩니다:

```bat
zig cc -target x86_64-windows-gnu -shared -O2 -o notesradar.dll src\notesradar.c
```

또는 `build.bat` 을 실행하십시오.

## 설치

1. `notesradar.dll` 과 `notesradar.ini` 를 `bm2dx.dll` 과 같은 폴더에 둡니다;
2. spice2x 실행 인자에 다음을 추가합니다:

   ```
   -k notesradar.dll
   ```

3. 게임을 시작하면 `notesradar.log` 가 생성됩니다. 정상 시 다음과 같이 기록됩니다:

   ```
   notesradar loaded (build ...; cap=1 pnr=1 2dx=0)
   cap removal: 3/3 sites patched
   addBinary hook installed ...
   all done
   ```

## 설정

`notesradar.ini`

```ini
EnableCap=1          ; 채보 레이더 상한 해제
EnablePlayerCap=1    ; PNR 집계 200.00 캡 (온라인에서는 반드시 1)
Enable2dxCompat=0    ; 2dxtra 호환 (실험적)
EnableRecalcKey=1    ; Ctrl+Alt+R 전체 재계산
```

## 알려진 제한

- **2dxtra 의 특수 채보 생성**(kichiku / kiraku / all scratch)은 채보 레이더를 읽어
  `200.00 이하` 를 가정합니다. `EnableCap` 이 켜진 상태에서 해당 채보 유형으로 전환하면
  충돌할 수 있습니다(본질적인 충돌). 필요하면 `EnableCap=0` 으로 바꾸고 게임을 재시작하십시오.
- `Enable2dxCompat` 은 실험적 기능입니다(2dxtra 의 쓰기 되돌림을 가로챔). 2dxtra 의 코드는
  Windows x64 ABI 를 따르지 않으며(r9 보존 가정, 호출 지점 스택 미정렬), 여러 시도에도
  불안정하여 **기본적으로 비활성화**되어 있습니다.
- 레이더 값이 200 을 넘으면 육각형이 프레임 밖에 그려집니다(게임이 `/200` 으로 정규화하여
  그리기 때문). 이는 예상된 동작입니다.

## 면책 조항

본 프로젝트는 **로컬 / 오프라인 환경에서의 연구 및 개인 사용**을 목적으로 합니다.
게임 본체를 합법적으로 소유하고 있어야 합니다.
본 플러그인 사용으로 인해 발생하는 데이터 손상, 계정 정지, 기타 결과에 대해
작성자는 책임지지 않습니다.
**온라인 대전이나 다른 플레이어에게 영향을 주는 용도로 사용하지 마십시오.**

## 라이선스

MIT
