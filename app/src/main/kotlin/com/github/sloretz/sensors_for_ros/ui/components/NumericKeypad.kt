package com.github.sloretz.sensors_for_ros.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun NumericKeypad(
    currentValue: Int,
    onDigit: (Int) -> Unit,
    onClear: () -> Unit,
    modifier: Modifier = Modifier
) {
    val buttonHeight = 56.dp

    fun isDigitAllowed(digit: Int): Boolean {
        val nextValue = if (currentValue < 0) digit else currentValue * 10 + digit
        return nextValue in 0..232
    }

    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        // Row 1: 1 2 3
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            for (digit in 1..3) {
                Button(
                    onClick = { onDigit(digit) },
                    enabled = isDigitAllowed(digit),
                    modifier = Modifier.weight(1f).height(buttonHeight)
                ) {
                    Text(digit.toString(), fontSize = 20.sp)
                }
            }
        }
        // Row 2: 4 5 6
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            for (digit in 4..6) {
                Button(
                    onClick = { onDigit(digit) },
                    enabled = isDigitAllowed(digit),
                    modifier = Modifier.weight(1f).height(buttonHeight)
                ) {
                    Text(digit.toString(), fontSize = 20.sp)
                }
            }
        }
        // Row 3: 7 8 9
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            for (digit in 7..9) {
                Button(
                    onClick = { onDigit(digit) },
                    enabled = isDigitAllowed(digit),
                    modifier = Modifier.weight(1f).height(buttonHeight)
                ) {
                    Text(digit.toString(), fontSize = 20.sp)
                }
            }
        }
        // Row 4: 0 Clear
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Button(
                onClick = { onDigit(0) },
                enabled = isDigitAllowed(0),
                modifier = Modifier.weight(1f).height(buttonHeight)
            ) {
                Text("0", fontSize = 20.sp)
            }
            OutlinedButton(
                onClick = onClear,
                modifier = Modifier.weight(2f).height(buttonHeight)
            ) {
                Text("Clear", fontSize = 20.sp)
            }
        }
    }
}
