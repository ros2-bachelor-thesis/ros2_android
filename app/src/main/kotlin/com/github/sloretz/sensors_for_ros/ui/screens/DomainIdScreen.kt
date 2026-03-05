package com.github.sloretz.sensors_for_ros.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.sloretz.sensors_for_ros.ui.components.NumericKeypad

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DomainIdScreen(
    networkInterfaces: List<String>,
    onStartRos: (domainId: Int, networkInterface: String) -> Unit
) {
    var domainId by remember { mutableIntStateOf(-1) }
    var selectedInterface by remember(networkInterfaces) {
        mutableStateOf(
            networkInterfaces.firstOrNull { it.startsWith("wlan") }
                ?: networkInterfaces.firstOrNull()
                ?: ""
        )
    }
    var dropdownExpanded by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text(
            text = "ROS_DOMAIN_ID",
            style = MaterialTheme.typography.headlineMedium
        )

        Text(
            text = if (domainId < 0) "---" else domainId.toString(),
            style = MaterialTheme.typography.displaySmall,
            modifier = Modifier.align(Alignment.CenterHorizontally)
        )

        NumericKeypad(
            currentValue = domainId,
            onDigit = { digit ->
                domainId = if (domainId < 0) digit else domainId * 10 + digit
            },
            onClear = { domainId = -1 }
        )

        Spacer(modifier = Modifier.height(8.dp))

        if (networkInterfaces.isNotEmpty()) {
            ExposedDropdownMenuBox(
                expanded = dropdownExpanded,
                onExpandedChange = { dropdownExpanded = it }
            ) {
                TextField(
                    value = selectedInterface,
                    onValueChange = {},
                    readOnly = true,
                    label = { Text("Network Interface") },
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = dropdownExpanded) },
                    modifier = Modifier.menuAnchor().fillMaxWidth()
                )
                ExposedDropdownMenu(
                    expanded = dropdownExpanded,
                    onDismissRequest = { dropdownExpanded = false }
                ) {
                    networkInterfaces.forEach { iface ->
                        DropdownMenuItem(
                            text = { Text(iface) },
                            onClick = {
                                selectedInterface = iface
                                dropdownExpanded = false
                            }
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.weight(1f))

        Button(
            onClick = {
                val id = if (domainId < 0) 0 else domainId
                onStartRos(id, selectedInterface)
            },
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp)
        ) {
            Text("Start ROS", style = MaterialTheme.typography.titleMedium)
        }
    }
}
