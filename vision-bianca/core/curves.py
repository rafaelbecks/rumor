import math


def clamp01(t: float) -> float:
    """Restringe un valor escalar al intervalo [0.0, 1.0]."""
    return max(0.0, min(1.0, float(t)))


def linear(t: float) -> float:
    """Función de transferencia lineal: f(t) = t."""
    return clamp01(t)


def exponential(t: float, exponent: float = 2.5) -> float:
    """Función de transferencia exponencial: f(t) = t ^ exponent."""
    t_clamped = clamp01(t)
    exp = max(0.01, float(exponent))
    return clamp01(math.pow(t_clamped, exp))


def sigmoid(t: float, steepness: float = 10.0) -> float:
    """Función de transferencia sigmoide normalizada exactamente entre 0.0 y 1.0."""
    t_clamped = clamp01(t)
    k = max(0.1, float(steepness))
    raw = 1.0 / (1.0 + math.exp(-k * (t_clamped - 0.5)))
    min_val = 1.0 / (1.0 + math.exp(k * 0.5))
    max_val = 1.0 / (1.0 + math.exp(-k * 0.5))
    return clamp01((raw - min_val) / (max_val - min_val))


def ease_in_out(t: float) -> float:
    """Función de transferencia cúbica suave (smoothstep / ease-in-out)."""
    t_clamped = clamp01(t)
    if t_clamped < 0.5:
        return 4.0 * t_clamped * t_clamped * t_clamped
    return 1.0 - math.pow(-2.0 * t_clamped + 2.0, 3.0) / 2.0


def evaluate_curve(t: float, curve_type: str = "exponential", exponent: float = 2.5) -> float:
    """Evalúa la curva de respuesta seleccionada sobre el valor normalizado t."""
    ctype = (curve_type or "exponential").strip().lower()
    if ctype == "linear":
        return linear(t)
    if ctype == "exponential":
        return exponential(t, exponent=exponent)
    if ctype == "sigmoid":
        # Usa exponent como factor de pendiente escalado si se desea ajustar
        steepness = max(4.0, float(exponent) * 4.0)
        return sigmoid(t, steepness=steepness)
    if ctype == "ease_in_out":
        return ease_in_out(t)
    return exponential(t, exponent=exponent)


class EMAFilter:
    """
    Filtro de Media Móvil Exponencial (Exponential Moving Average).
    Garantiza transiciones continuas y un decay orgánico suave hacia 0.0
    cuando los participantes abandonan el radio de detección (count == 0).
    """

    def __init__(self, alpha: float = 0.18, initial_value: float = 0.0) -> None:
        self.alpha = max(0.001, min(1.0, float(alpha)))
        self.value = clamp01(initial_value)

    def update(self, target_value: float) -> float:
        """
        Actualiza el estado del filtro hacia target_value frame a frame.
        Nunca salta abruptamente a cero; decae exponencialmente según alpha.
        """
        target = clamp01(target_value)
        self.value = (self.alpha * target) + ((1.0 - self.alpha) * self.value)
        if self.value < 1e-5 and target == 0.0:
            self.value = 0.0
        return self.value

    def reset(self, value: float = 0.0) -> None:
        """Reinicia manualmente el valor interno del filtro."""
        self.value = clamp01(value)
